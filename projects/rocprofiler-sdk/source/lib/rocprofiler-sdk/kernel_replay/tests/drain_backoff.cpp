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

#include "lib/rocprofiler-sdk/kernel_replay/drain_backoff.hpp"

#include <gtest/gtest.h>

#include <chrono>

using rocprofiler::kernel_replay::drain_backoff;

TEST(kernel_replay_drain_backoff, polls_back_to_back_first)
{
    auto backoff = drain_backoff{};
    for(uint32_t i = 0; i < drain_backoff::spin_polls; ++i)
        EXPECT_EQ(backoff.next().count(), 0) << "poll " << i;
    EXPECT_EQ(backoff.next(), std::chrono::nanoseconds{drain_backoff::min_sleep});
}

TEST(kernel_replay_drain_backoff, sleeps_double_up_to_the_old_interval)
{
    auto backoff = drain_backoff{};
    for(uint32_t i = 0; i < drain_backoff::spin_polls; ++i)
        backoff.next();

    auto previous = backoff.next();
    for(int i = 0; i < 32; ++i)
    {
        const auto current = backoff.next();
        EXPECT_GE(current, previous);
        EXPECT_LE(current, std::chrono::nanoseconds{drain_backoff::max_sleep});
        previous = current;
    }
    EXPECT_EQ(previous, std::chrono::nanoseconds{drain_backoff::max_sleep})
        << "a long wait settles at the 2 ms interval the drain used before";
}

TEST(kernel_replay_drain_backoff, total_pause_before_reaching_the_cap_is_bounded)
{
    // Reaching the 2 ms interval takes 64 yields plus 50 us .. 1.6 ms of sleeps, so a wait that
    // ends within ~3 ms never pays more than one doubling step beyond its own length.
    auto backoff = drain_backoff{};
    auto total   = std::chrono::nanoseconds{0};
    while(true)
    {
        const auto pause = backoff.next();
        if(pause == std::chrono::nanoseconds{drain_backoff::max_sleep}) break;
        total += pause;
    }
    EXPECT_LT(total, std::chrono::milliseconds{4});
}
