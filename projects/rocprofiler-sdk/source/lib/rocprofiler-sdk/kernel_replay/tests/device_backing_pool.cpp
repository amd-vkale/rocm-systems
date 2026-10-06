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

#include <gtest/gtest.h>

#include <cstdlib>
#include <map>
#include <set>
#include <vector>

namespace msnp = ::rocprofiler::kernel_replay::memory_snapshot;

namespace
{
// Host memory stands in for device memory; the pool never dereferences it.
struct fake_device_t
{
    size_t           allocations = 0;
    size_t           frees       = 0;
    size_t           live_bytes  = 0;
    std::set<void*>  live        = {};
    bool             fail        = false;
    std::vector<int> agents      = {};

    msnp::device_backing_pool make_pool()
    {
        return msnp::device_backing_pool{[this](uint64_t, uint64_t agent, size_t bytes) -> void* {
                                             if(fail) return nullptr;
                                             ++allocations;
                                             live_bytes += bytes;
                                             agents.push_back(static_cast<int>(agent));
                                             auto* ptr = std::malloc(bytes);
                                             live.insert(ptr);
                                             sizes[ptr] = bytes;
                                             return ptr;
                                         },
                                         [this](void* ptr) {
                                             ++frees;
                                             live_bytes -= sizes.at(ptr);
                                             live.erase(ptr);
                                             std::free(ptr);
                                         }};
    }

    std::map<void*, size_t> sizes = {};
};

constexpr uint64_t pool_a = 0xA;
constexpr uint64_t pool_b = 0xB;
constexpr uint64_t agent  = 7;
}  // namespace

TEST(kernel_replay_device_backing_pool, reuses_a_released_block_of_the_same_size)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    auto first = pool.acquire(pool_a, agent, 4096);
    ASSERT_TRUE(first);
    pool.release(pool_a, *first);
    auto second = pool.acquire(pool_a, agent, 4096);
    ASSERT_TRUE(second);

    EXPECT_EQ(second->ptr, first->ptr);
    EXPECT_EQ(dev.allocations, 1u);
    EXPECT_EQ(dev.agents.front(), static_cast<int>(agent));
    pool.release(pool_a, *second);
    pool.release_idle();
    EXPECT_EQ(dev.live_bytes, 0u);
}

TEST(kernel_replay_device_backing_pool, best_fit_within_twice_the_request)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    auto big = pool.acquire(pool_a, agent, 100 << 10);
    ASSERT_TRUE(big);
    pool.release(pool_a, *big);

    auto small = pool.acquire(pool_a, agent, 40 << 10);
    ASSERT_TRUE(small);
    EXPECT_NE(small->ptr, big->ptr) << "a 40 KiB region must not take a 100 KiB block";
    EXPECT_EQ(small->capacity, size_t{40 << 10});

    auto medium = pool.acquire(pool_a, agent, 60 << 10);
    ASSERT_TRUE(medium);
    EXPECT_EQ(medium->ptr, big->ptr) << "a 60 KiB region reuses the 100 KiB block";
    EXPECT_EQ(medium->capacity, size_t{100 << 10});

    pool.release(pool_a, *small);
    pool.release(pool_a, *medium);
    pool.release_idle();
    EXPECT_EQ(dev.live.size(), 0u);
}

TEST(kernel_replay_device_backing_pool, a_snapshot_frees_idle_blocks_it_did_not_take)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    // First snapshot: three regions.
    auto token = pool.begin_snapshot(pool_a);
    auto a     = *pool.acquire(pool_a, agent, 1 << 20);
    auto b     = *pool.acquire(pool_a, agent, 2 << 20);
    auto c     = *pool.acquire(pool_a, agent, 3 << 20);
    pool.end_snapshot(pool_a, token);
    pool.release(pool_a, a);
    pool.release(pool_a, b);
    pool.release(pool_a, c);
    EXPECT_EQ(pool.idle_bytes(pool_a), size_t{6 << 20});

    // Second snapshot: the 2 MiB region was freed by the application; only two regions remain.
    // The unused block is kept through this snapshot, in case the next one needs it.
    token   = pool.begin_snapshot(pool_a);
    auto a2 = *pool.acquire(pool_a, agent, 1 << 20);
    auto c2 = *pool.acquire(pool_a, agent, 3 << 20);
    EXPECT_EQ(pool.end_snapshot(pool_a, token), 0u);
    EXPECT_EQ(a2.ptr, a.ptr);
    EXPECT_EQ(c2.ptr, c.ptr);
    EXPECT_EQ(pool.idle_bytes(pool_a), size_t{2 << 20});
    pool.release(pool_a, a2);
    pool.release(pool_a, c2);

    // Third snapshot: the 2 MiB block has now stayed idle through two snapshots and is freed.
    token        = pool.begin_snapshot(pool_a);
    auto a3      = *pool.acquire(pool_a, agent, 1 << 20);
    auto c3      = *pool.acquire(pool_a, agent, 3 << 20);
    auto trimmed = pool.end_snapshot(pool_a, token);

    EXPECT_EQ(trimmed, size_t{2 << 20});
    EXPECT_EQ(pool.idle_bytes(pool_a), 0u) << "only the recent footprints stay allocated";
    EXPECT_EQ(dev.allocations, 3u);
    EXPECT_EQ(dev.frees, 1u);

    pool.release(pool_a, a3);
    pool.release(pool_a, c3);
    pool.release_idle();
    EXPECT_EQ(dev.live_bytes, 0u);
}

// Equal-size blocks must not take turns: if each snapshot took the block that has been idle the
// longest, a block no snapshot needs any more would be picked up again before it aged out.
TEST(kernel_replay_device_backing_pool, an_unneeded_block_of_a_needed_size_is_freed)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    // First snapshot: two regions of the same size.
    auto token = pool.begin_snapshot(pool_a);
    auto keep  = *pool.acquire(pool_a, agent, 4 << 20);
    auto gone  = *pool.acquire(pool_a, agent, 4 << 20);
    pool.end_snapshot(pool_a, token);
    pool.release(pool_a, keep);
    pool.release(pool_a, gone);

    // The application frees one of them; the next snapshots need a single 4 MiB block.
    auto trimmed = size_t{0};
    for(int i = 0; i < 2; ++i)
    {
        token      = pool.begin_snapshot(pool_a);
        auto block = *pool.acquire(pool_a, agent, 4 << 20);
        trimmed += pool.end_snapshot(pool_a, token);
        pool.release(pool_a, block);
    }

    EXPECT_EQ(trimmed, size_t{4 << 20});
    EXPECT_EQ(pool.idle_blocks(pool_a), 1u) << "only the block still in use stays allocated";
    EXPECT_EQ(dev.allocations, 2u);
    EXPECT_EQ(dev.frees, 1u);
    pool.release_idle();
    EXPECT_EQ(dev.live_bytes, 0u);
}

// Range replay holds an entry snapshot while it takes an exit snapshot of the same footprint, then
// releases both. The next range's entry snapshot reuses one set; the trim must leave the other for
// that range's exit snapshot instead of making it allocate again.
TEST(kernel_replay_device_backing_pool, alternating_entry_and_exit_snapshots_reuse_both_sets)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    for(int range = 0; range < 5; ++range)
    {
        auto entry_token = pool.begin_snapshot(pool_a);
        auto entry       = *pool.acquire(pool_a, agent, 8 << 20);
        pool.end_snapshot(pool_a, entry_token);

        auto exit_token = pool.begin_snapshot(pool_a);
        auto exit       = *pool.acquire(pool_a, agent, 8 << 20);
        pool.end_snapshot(pool_a, exit_token);

        pool.release(pool_a, entry);
        pool.release(pool_a, exit);
    }

    EXPECT_EQ(dev.allocations, 2u) << "the entry and exit backing are each allocated once";
    EXPECT_EQ(dev.frees, 0u);
    pool.release_idle();
    EXPECT_EQ(dev.live_bytes, 0u);
}

TEST(kernel_replay_device_backing_pool, blocks_released_during_a_snapshot_survive_its_trim)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    auto held  = *pool.acquire(pool_a, agent, 8192);
    auto token = pool.begin_snapshot(pool_a);
    // e.g. another snapshot of the same pool ends while this one is being taken
    pool.release(pool_a, held);
    pool.end_snapshot(pool_a, token);

    EXPECT_EQ(pool.idle_blocks(pool_a), 1u);
    EXPECT_EQ(dev.frees, 0u);
    pool.release_idle();
}

TEST(kernel_replay_device_backing_pool, pools_are_trimmed_independently)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    // GPU A's replay keeps its backing between dispatches...
    auto a = *pool.acquire(pool_a, agent, 1 << 20);
    pool.release(pool_a, a);

    // ...while GPU B snapshots and trims its own pool.
    auto token = pool.begin_snapshot(pool_b);
    auto b     = *pool.acquire(pool_b, agent, 1 << 20);
    pool.end_snapshot(pool_b, token);

    EXPECT_EQ(pool.idle_bytes(pool_a), size_t{1 << 20})
        << "a snapshot on one pool must not free another pool's retained backing";
    EXPECT_NE(a.ptr, b.ptr);
    pool.release(pool_b, b);
    pool.release_idle();
    EXPECT_EQ(dev.live_bytes, 0u);
}

TEST(kernel_replay_device_backing_pool, release_idle_frees_everything_idle_but_not_held_blocks)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();

    auto held = *pool.acquire(pool_a, agent, 4096);
    auto idle = *pool.acquire(pool_b, agent, 8192);
    pool.release(pool_b, idle);

    EXPECT_EQ(pool.release_idle(), 8192u);
    EXPECT_EQ(dev.live.count(held.ptr), 1u) << "backing in use by a live snapshot stays";
    EXPECT_EQ(dev.live.count(idle.ptr), 0u);

    pool.release(pool_a, held);
    EXPECT_EQ(pool.release_idle(), 4096u);
}

TEST(kernel_replay_device_backing_pool, allocation_failure_is_reported)
{
    auto dev  = fake_device_t{};
    auto pool = dev.make_pool();
    dev.fail  = true;
    EXPECT_FALSE(pool.acquire(pool_a, agent, 4096));
    EXPECT_FALSE(pool.acquire(pool_a, agent, 0));
}
