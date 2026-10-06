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

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace msnp = ::rocprofiler::kernel_replay::memory_snapshot;

namespace
{
msnp::module_variable_scan_t
make_scan(size_t count, bool incomplete = false)
{
    auto scan = msnp::module_variable_scan_t{};
    for(size_t i = 0; i < count; ++i)
        scan.found.push_back(
            msnp::module_variable_t{reinterpret_cast<void*>(0x1000 * (i + 1)), 8 * (i + 1)});
    scan.incomplete = incomplete;
    return scan;
}
}  // namespace

TEST(kernel_replay_module_variable_cache, same_generation_reuses_the_scan)
{
    auto cache = msnp::module_variable_cache{};
    auto calls = 0;
    auto scan  = [&]() {
        ++calls;
        return make_scan(3);
    };

    auto first  = cache.get(1, 7, scan);
    auto second = cache.get(1, 7, scan);

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(first.get(), second.get());
    ASSERT_EQ(second->found.size(), 3u);
    EXPECT_EQ(second->found[2].size, 24u);
}

TEST(kernel_replay_module_variable_cache, new_generation_rescans)
{
    auto cache = msnp::module_variable_cache{};
    auto calls = 0;
    auto count = size_t{1};
    auto scan  = [&]() {
        ++calls;
        return make_scan(count);
    };

    EXPECT_EQ(cache.get(1, 7, scan)->found.size(), 1u);
    count = 4;
    EXPECT_EQ(cache.get(1, 8, scan)->found.size(), 4u) << "a load/unload must invalidate the memo";
    EXPECT_EQ(cache.get(1, 8, scan)->found.size(), 4u);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(cache.scans(), 2u);
}

TEST(kernel_replay_module_variable_cache, incomplete_scan_is_not_cached)
{
    auto cache      = msnp::module_variable_cache{};
    auto calls      = 0;
    auto incomplete = true;
    auto scan       = [&]() {
        ++calls;
        return make_scan(2, incomplete);
    };

    EXPECT_TRUE(cache.get(1, 7, scan)->incomplete);
    incomplete = false;
    EXPECT_FALSE(cache.get(1, 7, scan)->incomplete)
        << "an incomplete scan declines replay, so it must be retried rather than remembered";
    EXPECT_FALSE(cache.get(1, 7, scan)->incomplete);
    EXPECT_EQ(calls, 2);
}

TEST(kernel_replay_module_variable_cache, incomplete_scan_drops_an_older_entry)
{
    auto cache      = msnp::module_variable_cache{};
    auto calls      = 0;
    auto incomplete = false;
    auto scan       = [&]() {
        ++calls;
        return make_scan(2, incomplete);
    };

    cache.get(1, 7, scan);
    incomplete = true;
    EXPECT_TRUE(cache.get(1, 8, scan)->incomplete);
    incomplete = false;
    EXPECT_FALSE(cache.get(1, 7, scan)->incomplete)
        << "returning to an older generation after a failed rescan must not reuse stale state";
    EXPECT_EQ(calls, 3);
}

TEST(kernel_replay_module_variable_cache, agents_are_cached_separately)
{
    auto cache = msnp::module_variable_cache{};
    auto calls = 0;

    auto a = cache.get(1, 7, [&]() {
        ++calls;
        return make_scan(1);
    });
    auto b = cache.get(2, 7, [&]() {
        ++calls;
        return make_scan(5);
    });

    EXPECT_EQ(calls, 2);
    EXPECT_EQ(a->found.size(), 1u);
    EXPECT_EQ(b->found.size(), 5u);
}

TEST(kernel_replay_module_variable_cache, a_held_scan_outlives_its_replacement)
{
    auto cache = msnp::module_variable_cache{};
    auto held  = cache.get(1, 7, []() { return make_scan(2); });
    cache.get(1, 8, []() { return make_scan(6); });
    cache.clear();

    ASSERT_EQ(held->found.size(), 2u) << "a snapshot in progress keeps the scan it started with";
    EXPECT_EQ(held->found[1].gpu_addr, reinterpret_cast<void*>(0x2000));
}

TEST(kernel_replay_module_variable_cache, concurrent_lookups_agree)
{
    auto cache = msnp::module_variable_cache{};
    auto sizes = std::vector<size_t>(8, 0);

    auto threads = std::vector<std::thread>{};
    for(size_t t = 0; t < sizes.size(); ++t)
    {
        threads.emplace_back([&, t]() {
            for(int i = 0; i < 1000; ++i)
                sizes[t] = cache.get(t % 2, 3, []() { return make_scan(4); })->found.size();
        });
    }
    for(auto& thr : threads)
        thr.join();

    for(auto size : sizes)
        EXPECT_EQ(size, 4u);
    EXPECT_LE(cache.scans(), sizes.size()) << "each agent settles on one cached scan";
}
