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

#include "lib/rocprofiler-sdk/kernel_replay/blit-copy.hpp"

#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace rocprofiler
{
namespace kernel_replay
{
// Minimal save/restore of device memory for kernel replay.
//
// snap(agent) copies every tracked device allocation owned by `agent` into snapshot backing and
// restore() copies it back. The replay path prefers GPU-local backing and falls back to host memory
// under device-memory pressure. This keeps each replay pass running against identical inputs,
// scoped to one agent so concurrent replays on other agents are unaffected.
namespace memory_snapshot
{
// Saved copy of a single device allocation.
struct mem_block_t
{
    mem_block_t() = default;
    ~mem_block_t();

    mem_block_t(const mem_block_t&) = delete;
    mem_block_t& operator=(const mem_block_t&) = delete;
    mem_block_t(mem_block_t&& rhs) noexcept;
    mem_block_t& operator=(mem_block_t&& rhs) noexcept;

    void*                 gpu_addr    = nullptr;  // live application allocation base pointer
    void*                 device_copy = nullptr;  // GPU-local snapshot backing when available
    hsa_amd_memory_pool_t device_pool{.handle = 0};
    size_t                device_capacity = 0;  // bytes behind device_copy, >= copy_size
    size_t                copy_size       = 0;
    std::vector<char>     host_copy;  // fallback backing when GPU-local allocation fails
    // true  = from the allocation tracker; re-check liveness before restoring (it can be freed).
    // false = module-scope variable in a loaded executable; always live, so restore
    // unconditionally.
    bool from_tracker = false;
    // false = backing is allocated but holds nothing yet; capture() fills it (deferred snap).
    bool captured = true;

    const void* saved_data() const
    {
        return device_copy ? device_copy : static_cast<const void*>(host_copy.data());
    }
};

// A captured set of device allocations for a single agent.
struct device_snapshot_t
{
    std::vector<mem_block_t> blocks;
    // false => capture was incomplete: snapshot-backing pressure, a failed copy, or HSA could not
    // enumerate the module-scope variables of every loaded executable. A partial snapshot must not
    // be restored, so the caller should decline replay and run the dispatch once instead.
    bool ok = true;

    bool empty() const { return blocks.empty(); }
};

// Copy (device->host) every tracked allocation owned by `agent`, plus every module-scope variable
// (__device__ / __constant__ global) visible to `agent` in the loaded executables. On success the
// returned snapshot has ok==true. It returns early with ok==false, so the caller can decline replay
// rather than restore a partial snapshot, when a region cannot be captured (host memory pressure --
// the host buffer allocation fails -- or a failed copy) or when the module-scope variables of a
// loaded executable cannot be enumerated. Host memory pressure is never fatal: a dispatch that
// cannot be snapshotted runs once instead of aborting the application.
device_snapshot_t
snap(hsa_agent_t agent);

// When snap() copies GPU-local regions.
enum class capture_mode
{
    immediate,  // every region is copied inside snap()
    deferred,   // GPU-local regions get their backing in snap() and are copied by capture()
};

// Prefer GPU-local snapshot backing from `gpu_pool`. A region falls back to host memory when its
// GPU-local allocation cannot be created. Internal allocations bypass the tracker, so a snapshot
// never captures its own backing. Host-backed regions are always copied inside snap().
device_snapshot_t
snap(hsa_agent_t           agent,
     hsa_amd_memory_pool_t gpu_pool,
     capture_mode          mode = capture_mode::immediate);

using batch_copy_fn_t = std::function<hsa_status_t(const std::vector<blit::copy_region_t>&)>;

// True when a deferred snap() left GPU-local blocks for capture() to copy.
bool
has_pending_capture(const device_snapshot_t& snapshot);

// Copy every block a deferred snap() left pending into its backing, as one batch: `submit` gets
// the live->backing copies of the pending blocks that are still allocated. It runs under the
// allocation inventory's read lock, so a concurrent free cannot retire a region mid-copy, and it
// must not return before the copies have completed. Pending blocks freed since snap() are dropped
// from the snapshot. Returns false when `submit` fails; the snapshot must then not be restored.
bool
capture(device_snapshot_t& snapshot, const batch_copy_fn_t& submit);

// Synchronous copy of each region, for a batch the blit could not take.
hsa_status_t
copy_regions(const std::vector<blit::copy_region_t>& regions);

// GPU-local backing kept idle for reuse by the next snapshot from `pool`, for tests.
size_t
retained_backing_bytes(hsa_amd_memory_pool_t pool);

// Copy each saved region back to its live device allocation. A region freed after snap is skipped.
// A failed copy returns false immediately because the snapshot is then only partially applied.
bool
restore(const device_snapshot_t& snapshot);

// Restore GPU-local blocks through a batch copy callback. Host-backed fallback blocks retain the
// synchronous ROCr path.
bool
restore(const device_snapshot_t& snapshot, const batch_copy_fn_t& batch_copy);
}  // namespace memory_snapshot
}  // namespace kernel_replay
}  // namespace rocprofiler
