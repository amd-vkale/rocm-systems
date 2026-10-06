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
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rocprofiler
{
namespace kernel_replay
{
namespace memory_snapshot
{
// A module-scope variable (__device__ / __constant__ global) discovered in a loaded executable.
struct module_variable_t
{
    void*  gpu_addr = nullptr;
    size_t size     = 0;
};

// Result of enumerating module-scope variables. `incomplete` means HSA could not be asked about at
// least one executable or symbol, so the set below may be missing a writable __device__ global.
// Treated as a failed snapshot rather than a partial one: a variable we never captured is a
// variable we never restore, and passes 2..N would silently read state accumulated by pass 1.
struct module_variable_scan_t
{
    std::vector<module_variable_t> found      = {};
    bool                           incomplete = false;
};

// Per-agent memo of the module-variable scan. A variable's address and size are fixed once its
// executable is frozen, so the scan only changes when a code object is loaded or unloaded; the
// caller passes a generation that changes exactly then
// (code_object::loaded_code_objects_generation). The values are still read at snap time, so
// constant memory written after load is captured.
class module_variable_cache
{
public:
    using scan_ptr_t = std::shared_ptr<const module_variable_scan_t>;
    using scan_fn_t  = std::function<module_variable_scan_t()>;

    // Returns the scan for `agent`, running `scan` only when nothing is cached for `generation`.
    // An incomplete scan is returned but never cached, so the next call retries it.
    scan_ptr_t get(uint64_t agent, uint64_t generation, const scan_fn_t& scan);

    void clear();

    // Number of times get() ran `scan`, for tests.
    uint64_t scans() const;

private:
    struct entry_t
    {
        uint64_t   generation = 0;
        scan_ptr_t scan       = {};
    };

    mutable std::mutex                    m_mutex   = {};
    std::unordered_map<uint64_t, entry_t> m_entries = {};
    uint64_t                              m_scans   = 0;
};
}  // namespace memory_snapshot
}  // namespace kernel_replay
}  // namespace rocprofiler
