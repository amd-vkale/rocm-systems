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

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace rocprofiler
{
namespace kernel_replay
{
namespace memory_snapshot
{
// GPU-local snapshot backing retained between replayed dispatches, per memory pool.
//
// acquire() takes the best-fitting idle block of at least the request and at most twice it, so a
// small region cannot hold a large block a later region would have to allocate again, and
// allocates when nothing fits. Of equally good fits it takes the one released last. release()
// returns a block to its pool's idle list. A snapshot brackets its acquisitions with
// begin_snapshot()/end_snapshot() on its pool; end_snapshot() frees every block of that pool that
// has stayed idle through the previous snapshot and this one. A pool therefore keeps the backing of
// its two most recent snapshots, instead of every region size ever seen: enough for range replay,
// which alternates an entry and an exit snapshot of the same footprint, to reuse both, and for
// kernel replay's one snapshot per dispatch. A snapshot on one GPU never trims another GPU's pool.
// release_idle() frees every idle block, for when the application needs the memory.
class device_backing_pool
{
public:
    using allocate_fn_t = std::function<void*(uint64_t pool, uint64_t agent, size_t bytes)>;
    using free_fn_t     = std::function<void(void* ptr)>;

    struct block_t
    {
        void*  ptr      = nullptr;
        size_t capacity = 0;
    };

    device_backing_pool(allocate_fn_t allocate, free_fn_t free);

    std::optional<block_t> acquire(uint64_t pool, uint64_t agent, size_t bytes);
    void                   release(uint64_t pool, block_t block);

    uint64_t begin_snapshot(uint64_t pool);
    size_t   end_snapshot(uint64_t pool, uint64_t token);  // returns the bytes freed
    size_t   release_idle();                               // returns the bytes freed

    size_t idle_bytes(uint64_t pool) const;
    size_t idle_blocks(uint64_t pool) const;

private:
    struct idle_t
    {
        void*    ptr      = nullptr;
        uint64_t released = 0;  // the pool's epoch when the block went idle
    };

    struct pool_state_t
    {
        std::multimap<size_t, idle_t> idle  = {};  // capacity -> block
        uint64_t                      epoch = 0;
    };

    allocate_fn_t                              m_allocate = {};
    free_fn_t                                  m_free     = {};
    mutable std::mutex                         m_mutex    = {};
    std::unordered_map<uint64_t, pool_state_t> m_pools    = {};
};
}  // namespace memory_snapshot
}  // namespace kernel_replay
}  // namespace rocprofiler
