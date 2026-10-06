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

#include "blit-copy-kernel.hpp"

#include <hip/hip_runtime.h>

#include <cstddef>
#include <cstdint>

using rocprofiler::kernel_replay::blit::kernel_abi::bytes_per_item;
using rocprofiler::kernel_replay::blit::kernel_abi::copy_descriptor_t;
using rocprofiler::kernel_replay::blit::kernel_abi::kernel_args_t;
using rocprofiler::kernel_replay::blit::kernel_abi::small_item_offset;
using rocprofiler::kernel_replay::blit::kernel_abi::small_region_index;
using rocprofiler::kernel_replay::blit::kernel_abi::tile_item_offset;
using rocprofiler::kernel_replay::blit::kernel_abi::workgroup_size;

using copy_vector_t       = std::uint32_t __attribute__((ext_vector_type(4)));
using global_byte_t       = std::byte __attribute__((address_space(1)));
using global_descriptor_t = copy_descriptor_t __attribute__((address_space(1)));

static_assert(bytes_per_item == sizeof(copy_vector_t));

// Tiled region containing `tile`, searched among the descriptors [first, last) after the small
// regions. Every input is uniform across the wave, so this runs on the scalar unit.
__device__ __forceinline__ copy_descriptor_t
find_tiled_region(const global_descriptor_t* descriptors,
                  std::uint64_t              first,
                  std::uint64_t              last,
                  std::uint64_t              tile)
{
    while(first < last)
    {
        const auto middle     = first + (last - first) / 2;
        const auto descriptor = descriptors[middle];
        if(tile < descriptor.first_tile + descriptor.tile_count)
            last = middle;
        else
            first = middle + 1;
    }
    return descriptors[first];
}

__device__ __forceinline__ void
copy_item(const copy_descriptor_t& copy, std::uint64_t offset)
{
    const auto* src = reinterpret_cast<const global_byte_t*>(copy.source_address);
    auto*       dst = reinterpret_cast<global_byte_t*>(copy.destination_address);

    if(offset + sizeof(copy_vector_t) <= copy.size)
    {
        auto value = copy_vector_t{};
        __builtin_memcpy(&value, src + offset, sizeof(value));
        __builtin_memcpy(dst + offset, &value, sizeof(value));
        return;
    }

    for(auto i = offset; i < copy.size; ++i)
        dst[i] = src[i];
}

// One region of at most small_region_limit bytes per group of lanes_per_small_region lanes,
// indexed directly: no tile and no descriptor search per region.
__device__ __forceinline__ void
copy_small(const kernel_args_t& args, std::uint64_t group)
{
    const auto thread = static_cast<std::uint32_t>(threadIdx.x);
    const auto region = small_region_index(group, thread);
    if(region >= args.small_count) return;

    const auto* descriptors = reinterpret_cast<const global_descriptor_t*>(args.descriptors_address);
    const auto  copy        = descriptors[region];
    for(auto step = std::uint64_t{0};; ++step)
    {
        const auto offset = small_item_offset(thread, step);
        if(offset >= copy.size) return;
        copy_item(copy, offset);
    }
}

__device__ __forceinline__ void
copy_tile(const kernel_args_t& args, std::uint64_t tile)
{
    const auto* descriptors = reinterpret_cast<const global_descriptor_t*>(args.descriptors_address);
    const auto  copy =
        find_tiled_region(descriptors, args.small_count, args.descriptor_count, tile);
    const auto offset =
        tile_item_offset(tile - copy.first_tile, static_cast<std::uint32_t>(threadIdx.x));
    if(offset < copy.size) copy_item(copy, offset);
}

__device__ __forceinline__ void
copy_group(const kernel_args_t& args, std::uint64_t group)
{
    if(group < args.small_groups)
        copy_small(args, group);
    else
        copy_tile(args, group - args.small_groups);
}

extern "C" __global__ __launch_bounds__(workgroup_size) void kernel_replay_blit(kernel_args_t args)
{
    const auto group = static_cast<std::uint64_t>(blockIdx.x);
    if(group < args.total_groups) copy_group(args, group);
}

extern "C" __global__ __launch_bounds__(workgroup_size) void kernel_replay_blit_stride(
    kernel_args_t args)
{
    for(auto group = static_cast<std::uint64_t>(blockIdx.x); group < args.total_groups;
        group += args.launched_groups)
        copy_group(args, group);
}
