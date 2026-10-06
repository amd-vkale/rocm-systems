// MIT License
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "lib/rocprofiler-sdk/kernel_replay/device_backing_pool.hpp"

#include <iterator>
#include <limits>
#include <utility>
#include <vector>

namespace rocprofiler
{
namespace kernel_replay
{
namespace memory_snapshot
{
device_backing_pool::device_backing_pool(allocate_fn_t allocate, free_fn_t free)
: m_allocate{std::move(allocate)}
, m_free{std::move(free)}
{}

std::optional<device_backing_pool::block_t>
device_backing_pool::acquire(uint64_t pool, uint64_t agent, size_t bytes)
{
    if(bytes == 0) return std::nullopt;

    {
        auto _lk = std::lock_guard<std::mutex>{m_mutex};
        if(auto pitr = m_pools.find(pool); pitr != m_pools.end())
        {
            auto&      idle  = pitr->second.idle;
            const auto limit = (bytes > std::numeric_limits<size_t>::max() / 2)
                                   ? std::numeric_limits<size_t>::max()
                                   : 2 * bytes;
            if(auto itr = idle.lower_bound(bytes); itr != idle.end() && itr->first <= limit)
            {
                // Equal capacities sit in release order. Taking the last one keeps reusing the
                // blocks snapshots still need, so the others stay idle long enough to be trimmed.
                itr        = std::prev(idle.upper_bound(itr->first));
                auto block = block_t{itr->second.ptr, itr->first};
                idle.erase(itr);
                return block;
            }
        }
    }

    // Allocate outside the lock: a large device allocation can take milliseconds.
    auto* ptr = m_allocate(pool, agent, bytes);
    if(!ptr) return std::nullopt;
    return block_t{ptr, bytes};
}

void
device_backing_pool::release(uint64_t pool, block_t block)
{
    if(!block.ptr) return;
    auto  _lk   = std::lock_guard<std::mutex>{m_mutex};
    auto& state = m_pools[pool];
    state.idle.emplace(block.capacity, idle_t{block.ptr, state.epoch});
}

uint64_t
device_backing_pool::begin_snapshot(uint64_t pool)
{
    auto _lk = std::lock_guard<std::mutex>{m_mutex};
    return ++m_pools[pool].epoch;
}

size_t
device_backing_pool::end_snapshot(uint64_t pool, uint64_t token)
{
    auto doomed = std::vector<void*>{};
    auto freed  = size_t{0};
    {
        auto _lk  = std::lock_guard<std::mutex>{m_mutex};
        auto pitr = m_pools.find(pool);
        if(pitr == m_pools.end()) return 0;
        auto& idle = pitr->second.idle;
        for(auto itr = idle.begin(); itr != idle.end();)
        {
            // Idle since before the previous snapshot of this pool began, and neither that snapshot
            // nor this one took it.
            if(itr->second.released + 1 < token)
            {
                freed += itr->first;
                doomed.emplace_back(itr->second.ptr);
                itr = idle.erase(itr);
            }
            else
            {
                ++itr;
            }
        }
    }
    for(auto* ptr : doomed)
        m_free(ptr);
    return freed;
}

size_t
device_backing_pool::release_idle()
{
    auto doomed = std::vector<void*>{};
    auto freed  = size_t{0};
    {
        auto _lk = std::lock_guard<std::mutex>{m_mutex};
        for(auto& [pool, state] : m_pools)
        {
            for(auto& [capacity, entry] : state.idle)
            {
                freed += capacity;
                doomed.emplace_back(entry.ptr);
            }
            state.idle.clear();
        }
    }
    for(auto* ptr : doomed)
        m_free(ptr);
    return freed;
}

size_t
device_backing_pool::idle_bytes(uint64_t pool) const
{
    auto _lk = std::lock_guard<std::mutex>{m_mutex};
    auto itr = m_pools.find(pool);
    if(itr == m_pools.end()) return 0;
    auto total = size_t{0};
    for(const auto& entry : itr->second.idle)
        total += entry.first;
    return total;
}

size_t
device_backing_pool::idle_blocks(uint64_t pool) const
{
    auto _lk = std::lock_guard<std::mutex>{m_mutex};
    auto itr = m_pools.find(pool);
    return (itr == m_pools.end()) ? 0 : itr->second.idle.size();
}
}  // namespace memory_snapshot
}  // namespace kernel_replay
}  // namespace rocprofiler
