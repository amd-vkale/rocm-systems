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

#include <cstdint>

#if defined(__HIP__) || defined(__HIPCC__)
#    define ROCPROFILER_KERNEL_REPLAY_BLIT_HD __host__ __device__
#else
#    define ROCPROFILER_KERNEL_REPLAY_BLIT_HD
#endif

namespace rocprofiler
{
namespace kernel_replay
{
namespace blit
{
namespace kernel_abi
{
inline constexpr auto bytes_per_item = std::uint64_t{16};
inline constexpr auto workgroup_size = std::uint16_t{1024};
inline constexpr auto bytes_per_tile = bytes_per_item * workgroup_size;
// A region up to small_region_limit bytes gets one group of lanes_per_small_region lanes instead of
// tiles, so a footprint of many small regions (module variables, small allocations) does not
// launch a whole workgroup, and a descriptor search, per region.
inline constexpr auto lanes_per_small_region = std::uint32_t{64};
inline constexpr auto small_regions_per_group =
    std::uint32_t{workgroup_size} / lanes_per_small_region;
inline constexpr auto small_region_limit = std::uint64_t{16384};

struct copy_descriptor_t
{
    std::uint64_t source_address      = 0;
    std::uint64_t destination_address = 0;
    std::uint64_t size                = 0;
    std::uint64_t first_tile          = 0;  // tiled regions only
    std::uint64_t tile_count          = 0;  // tiled regions only
};

// Workgroups [0, small_groups) copy the small regions, small_regions_per_group each; the rest copy
// one tile of the regions after them. The stride kernel launches fewer workgroups than
// total_groups and loops.
struct kernel_args_t
{
    std::uint64_t descriptors_address = 0;  // small regions first, then tiled regions
    std::uint64_t descriptor_count    = 0;
    std::uint64_t small_count         = 0;
    std::uint64_t small_groups        = 0;
    std::uint64_t total_groups        = 0;
    std::uint64_t launched_groups     = 0;
};

static_assert(sizeof(copy_descriptor_t) == 40);
static_assert(sizeof(kernel_args_t) == 48);

// Byte offset of a tile thread's 16-byte item within its region.
ROCPROFILER_KERNEL_REPLAY_BLIT_HD inline constexpr std::uint64_t
tile_item_offset(std::uint64_t tile_in_region, std::uint32_t thread)
{
    return tile_in_region * bytes_per_tile + std::uint64_t{thread} * bytes_per_item;
}

// Index of the small region a lane of workgroup `group` copies (may be past small_count).
ROCPROFILER_KERNEL_REPLAY_BLIT_HD inline constexpr std::uint64_t
small_region_index(std::uint64_t group, std::uint32_t thread)
{
    return group * small_regions_per_group + thread / lanes_per_small_region;
}

// Byte offset of a small-region lane's `step`-th 16-byte item.
ROCPROFILER_KERNEL_REPLAY_BLIT_HD inline constexpr std::uint64_t
small_item_offset(std::uint32_t thread, std::uint64_t step)
{
    return (step * lanes_per_small_region + thread % lanes_per_small_region) * bytes_per_item;
}
}  // namespace kernel_abi
}  // namespace blit
}  // namespace kernel_replay
}  // namespace rocprofiler
