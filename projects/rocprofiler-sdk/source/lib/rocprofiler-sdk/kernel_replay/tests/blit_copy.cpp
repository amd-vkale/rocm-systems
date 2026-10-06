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

// The replay blit's work decomposition: plan_copy() puts small regions first and tiles the rest,
// and the kernel maps every workgroup and lane onto those descriptors. The host tests replay that
// mapping (the same index helpers the kernel uses) and require every byte of every region to be
// copied exactly once; the GPU tests load the code object the runtime itself loads and run its
// kernels on buffers with guard bytes.

#include "lib/rocprofiler-sdk/kernel_replay/blit-copy.hpp"

#include <gtest/gtest.h>
#include <hip/hip_runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace blit = ::rocprofiler::kernel_replay::blit;
namespace kabi = ::rocprofiler::kernel_replay::blit::kernel_abi;

namespace
{
// Fake, well-separated addresses: the host tests never dereference them.
std::vector<blit::copy_region_t>
fake_regions(const std::vector<size_t>& sizes)
{
    auto out  = std::vector<blit::copy_region_t>{};
    auto base = uintptr_t{1} << 40;
    for(auto size : sizes)
    {
        out.push_back(blit::copy_region_t{reinterpret_cast<void*>(base + (uintptr_t{1} << 36)),
                                          reinterpret_cast<const void*>(base),
                                          size});
        base += uintptr_t{1} << 32;
    }
    return out;
}

size_t
find_tiled(const std::vector<kabi::copy_descriptor_t>& d, size_t first, size_t last, uint64_t tile)
{
    while(first < last)
    {
        const auto middle = first + (last - first) / 2;
        if(tile < d[middle].first_tile + d[middle].tile_count)
            last = middle;
        else
            first = middle + 1;
    }
    return first;
}

// Host replay of the kernel's mapping; returns per-descriptor byte coverage counts.
std::vector<std::vector<uint8_t>>
coverage(const blit::copy_plan_t& plan)
{
    const auto& d    = plan.descriptors;
    const auto& args = plan.args;
    auto        hits = std::vector<std::vector<uint8_t>>{};
    for(const auto& desc : d)
        hits.emplace_back(desc.size, 0);

    auto mark = [&](size_t idx, uint64_t begin, uint64_t end) {
        for(auto b = begin; b < end; ++b)
            ++hits.at(idx).at(b);
    };

    for(uint64_t group = 0; group < args.total_groups; ++group)
    {
        for(uint32_t thread = 0; thread < kabi::workgroup_size; ++thread)
        {
            if(group < args.small_groups)
            {
                const auto region = kabi::small_region_index(group, thread);
                if(region >= args.small_count) continue;
                for(uint64_t step = 0;; ++step)
                {
                    const auto offset = kabi::small_item_offset(thread, step);
                    if(offset >= d[region].size) break;
                    mark(region, offset, std::min(offset + kabi::bytes_per_item, d[region].size));
                    if(offset + kabi::bytes_per_item > d[region].size) break;
                }
                continue;
            }
            const auto tile   = group - args.small_groups;
            const auto region = find_tiled(d, args.small_count, args.descriptor_count, tile);
            const auto offset = kabi::tile_item_offset(tile - d[region].first_tile, thread);
            if(offset < d[region].size)
                mark(region, offset, std::min(offset + kabi::bytes_per_item, d[region].size));
        }
    }
    return hits;
}

void
expect_exact_coverage(const std::vector<size_t>& sizes)
{
    auto plan = blit::plan_copy(fake_regions(sizes), 304);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->descriptors.size(), sizes.size());
    auto hits = coverage(*plan);
    for(size_t i = 0; i < hits.size(); ++i)
        for(size_t b = 0; b < hits[i].size(); ++b)
            ASSERT_EQ(hits[i][b], 1)
                << "descriptor " << i << " (" << hits[i].size() << " bytes) byte " << b;
}
}  // namespace

TEST(kernel_replay_blit_plan, small_regions_come_first_and_are_not_tiled)
{
    const auto sizes = std::vector<size_t>{1 << 20, 4, kabi::small_region_limit, 100, 3 << 20};
    auto       plan  = blit::plan_copy(fake_regions(sizes), 304);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->args.small_count, 3u);
    EXPECT_EQ(plan->args.small_groups, 1u);
    for(uint64_t i = 0; i < plan->args.small_count; ++i)
        EXPECT_LE(plan->descriptors[i].size, kabi::small_region_limit);
    for(auto i = plan->args.small_count; i < plan->descriptors.size(); ++i)
        EXPECT_GT(plan->descriptors[i].size, kabi::small_region_limit);
    EXPECT_EQ(plan->args.total_groups,
              plan->args.small_groups + (1u << 20) / kabi::bytes_per_tile +
                  (3u << 20) / kabi::bytes_per_tile);
    EXPECT_FALSE(plan->use_stride);
}

TEST(kernel_replay_blit_plan, many_tiny_regions_share_workgroups)
{
    auto sizes = std::vector<size_t>(20000, 0);
    for(size_t i = 0; i < sizes.size(); ++i)
        sizes[i] = 4 * (1 + i % 7);
    auto plan = blit::plan_copy(fake_regions(sizes), 304);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->args.small_count, sizes.size());
    EXPECT_EQ(plan->args.total_groups,
              (sizes.size() + kabi::small_regions_per_group - 1) / kabi::small_regions_per_group)
        << "a tiny region must not cost a whole workgroup";
}

TEST(kernel_replay_blit_plan, every_byte_is_copied_exactly_once)
{
    expect_exact_coverage({1});
    expect_exact_coverage({15, 16, 17, 1023, 1024, 1025});
    expect_exact_coverage({kabi::small_region_limit, kabi::small_region_limit + 1});
    expect_exact_coverage(
        {kabi::bytes_per_tile - 1, kabi::bytes_per_tile, kabi::bytes_per_tile + 1});
    expect_exact_coverage({3 * kabi::bytes_per_tile + 4099, 7, 2 * kabi::bytes_per_tile});

    auto rng   = std::mt19937_64{12345};
    auto sizes = std::vector<size_t>{};
    for(int i = 0; i < 40; ++i)
        sizes.push_back(1 + rng() % (3 * kabi::bytes_per_tile));
    expect_exact_coverage(sizes);
}

TEST(kernel_replay_blit_plan, rejects_empty_and_null_regions)
{
    EXPECT_FALSE(blit::plan_copy({}, 304));
    EXPECT_FALSE(blit::plan_copy({blit::copy_region_t{nullptr, nullptr, 8}}, 304));
    EXPECT_FALSE(blit::plan_copy(fake_regions({0}), 304));
}

namespace
{
struct device_buffers_t
{
    std::vector<size_t>      sizes   = {};
    std::vector<size_t>      offsets = {};  // into src/dst; regions are separated by guard bytes
    size_t                   total   = 0;
    uint8_t*                 src     = nullptr;
    uint8_t*                 dst     = nullptr;
    kabi::copy_descriptor_t* descriptors = nullptr;
};

constexpr uint8_t guard_byte = 0xCD;
constexpr size_t  guard_size = 64;

struct blit_module_t
{
    hipModule_t   module = nullptr;
    hipFunction_t full   = nullptr;
    hipFunction_t stride = nullptr;
};

// The code object the build produced for this GPU, the same file the runtime loads.
const blit_module_t&
blit_module()
{
    static const auto value = [] {
        auto out   = blit_module_t{};
        int  count = 0;
        if(hipGetDeviceCount(&count) != hipSuccess || count == 0) return out;
        auto prop = hipDeviceProp_t{};
        if(hipGetDeviceProperties(&prop, 0) != hipSuccess) return out;
        auto arch = std::string{prop.gcnArchName};
        arch      = arch.substr(0, arch.find(':'));
        const auto path =
            std::string{KERNEL_REPLAY_BLIT_DIR} + "/" + arch + "_kernel_replay_blit.hsaco";
        if(hipModuleLoad(&out.module, path.c_str()) != hipSuccess) return blit_module_t{};
        if(hipModuleGetFunction(&out.full, out.module, "kernel_replay_blit") != hipSuccess ||
           hipModuleGetFunction(&out.stride, out.module, "kernel_replay_blit_stride") != hipSuccess)
            return blit_module_t{};
        return out;
    }();
    return value;
}

bool
have_gpu()
{
    return blit_module().full != nullptr;
}

hipError_t
launch_blit(const kabi::kernel_args_t& args, bool stride)
{
    auto  local    = args;
    auto  size     = sizeof(local);
    void* config[] = {HIP_LAUNCH_PARAM_BUFFER_POINTER,
                      &local,
                      HIP_LAUNCH_PARAM_BUFFER_SIZE,
                      &size,
                      HIP_LAUNCH_PARAM_END};
    auto  status   = hipModuleLaunchKernel(stride ? blit_module().stride : blit_module().full,
                                        static_cast<unsigned int>(args.launched_groups),
                                        1,
                                        1,
                                        kabi::workgroup_size,
                                        1,
                                        1,
                                        0,
                                        nullptr,
                                        nullptr,
                                        config);
    if(status != hipSuccess) return status;
    return hipDeviceSynchronize();
}

// Copies regions of `sizes` (each starting `misalign` bytes past a 16-byte boundary) with the
// real kernel and checks every byte inside the regions and every guard byte around them.
void
run_and_check(const std::vector<size_t>& sizes, size_t misalign, bool stride, uint64_t groups)
{
    auto bufs  = device_buffers_t{};
    bufs.sizes = sizes;
    for(auto size : sizes)
    {
        bufs.total += guard_size + misalign;
        bufs.offsets.push_back(bufs.total);
        bufs.total += size;
    }
    bufs.total += guard_size;

    auto pattern = std::vector<uint8_t>(bufs.total);
    for(size_t i = 0; i < pattern.size(); ++i)
        pattern[i] = static_cast<uint8_t>((i * 131 + 7) & 0xFF);

    ASSERT_EQ(hipMalloc(&bufs.src, bufs.total), hipSuccess);
    ASSERT_EQ(hipMalloc(&bufs.dst, bufs.total), hipSuccess);
    ASSERT_EQ(hipMemcpy(bufs.src, pattern.data(), bufs.total, hipMemcpyHostToDevice), hipSuccess);
    ASSERT_EQ(hipMemset(bufs.dst, guard_byte, bufs.total), hipSuccess);

    auto regions = std::vector<blit::copy_region_t>{};
    for(size_t i = 0; i < sizes.size(); ++i)
        regions.push_back(
            blit::copy_region_t{bufs.dst + bufs.offsets[i], bufs.src + bufs.offsets[i], sizes[i]});

    auto plan = blit::plan_copy(regions, 304);
    ASSERT_TRUE(plan);
    const auto bytes = plan->descriptors.size() * sizeof(kabi::copy_descriptor_t);
    ASSERT_EQ(hipMalloc(&bufs.descriptors, bytes), hipSuccess);
    ASSERT_EQ(hipMemcpy(bufs.descriptors, plan->descriptors.data(), bytes, hipMemcpyHostToDevice),
              hipSuccess);

    auto args                = plan->args;
    args.descriptors_address = reinterpret_cast<uint64_t>(bufs.descriptors);
    if(stride) args.launched_groups = std::min<uint64_t>(groups, args.total_groups);
    ASSERT_EQ(launch_blit(args, stride), hipSuccess);

    auto result = std::vector<uint8_t>(bufs.total);
    ASSERT_EQ(hipMemcpy(result.data(), bufs.dst, bufs.total, hipMemcpyDeviceToHost), hipSuccess);

    auto inside = std::vector<bool>(bufs.total, false);
    for(size_t i = 0; i < sizes.size(); ++i)
        std::fill_n(inside.begin() + bufs.offsets[i], sizes[i], true);
    for(size_t b = 0; b < bufs.total; ++b)
    {
        if(inside[b])
            ASSERT_EQ(result[b], pattern[b]) << "byte " << b << " inside a region";
        else
            ASSERT_EQ(result[b], guard_byte) << "byte " << b << " outside every region";
    }

    EXPECT_EQ(hipFree(bufs.descriptors), hipSuccess);
    EXPECT_EQ(hipFree(bufs.dst), hipSuccess);
    EXPECT_EQ(hipFree(bufs.src), hipSuccess);
}
}  // namespace

TEST(kernel_replay_blit_kernel, mixed_sizes_copy_exactly)
{
    if(!have_gpu()) GTEST_SKIP() << "no HIP GPU or no blit code object for it";
    auto rng   = std::mt19937_64{777};
    auto sizes = std::vector<size_t>{};
    for(int i = 0; i < 64; ++i)
        sizes.push_back(1 + rng() % 200000);
    for(auto misalign : {size_t{0}, size_t{4}, size_t{8}})
        run_and_check(sizes, misalign, false, 0);
}

TEST(kernel_replay_blit_kernel, many_tiny_regions_copy_exactly)
{
    if(!have_gpu()) GTEST_SKIP() << "no HIP GPU or no blit code object for it";
    auto sizes = std::vector<size_t>(5000, 0);
    for(size_t i = 0; i < sizes.size(); ++i)
        sizes[i] = 1 + (i * 13) % 64;
    run_and_check(sizes, 4, false, 0);
}

TEST(kernel_replay_blit_kernel, large_region_copies_exactly)
{
    if(!have_gpu()) GTEST_SKIP() << "no HIP GPU or no blit code object for it";
    run_and_check({(size_t{64} << 20) + 12, 5, 70000}, 0, false, 0);
}

TEST(kernel_replay_blit_kernel, stride_kernel_loops_over_every_group)
{
    if(!have_gpu()) GTEST_SKIP() << "no HIP GPU or no blit code object for it";
    auto sizes = std::vector<size_t>{(size_t{8} << 20) + 3, 17, 33, 16384, 16385, 900000};
    for(size_t i = 0; i < 300; ++i)
        sizes.push_back(1 + i % 40);
    for(uint64_t groups : {uint64_t{1}, uint64_t{3}, uint64_t{64}})
        run_and_check(sizes, 4, true, groups);
}
