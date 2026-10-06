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

#include "lib/rocprofiler-sdk/kernel_replay/module_variable_cache.hpp"

#include <utility>

namespace rocprofiler
{
namespace kernel_replay
{
namespace memory_snapshot
{
module_variable_cache::scan_ptr_t
module_variable_cache::get(uint64_t agent, uint64_t generation, const scan_fn_t& scan)
{
    {
        auto _lk = std::lock_guard<std::mutex>{m_mutex};
        if(auto itr = m_entries.find(agent);
           itr != m_entries.end() && itr->second.generation == generation)
            return itr->second.scan;
    }

    // Scan outside the lock: replays on different agents snapshot concurrently, and a scan walks
    // every symbol of every loaded executable.
    auto result = std::make_shared<const module_variable_scan_t>(scan());

    auto _lk = std::lock_guard<std::mutex>{m_mutex};
    ++m_scans;
    if(result->incomplete)
        m_entries.erase(agent);
    else
        m_entries[agent] = entry_t{generation, result};
    return result;
}

void
module_variable_cache::clear()
{
    auto _lk = std::lock_guard<std::mutex>{m_mutex};
    m_entries.clear();
}

uint64_t
module_variable_cache::scans() const
{
    auto _lk = std::lock_guard<std::mutex>{m_mutex};
    return m_scans;
}
}  // namespace memory_snapshot
}  // namespace kernel_replay
}  // namespace rocprofiler
