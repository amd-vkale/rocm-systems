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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

namespace rocprofiler
{
namespace kernel_replay
{
// Wait schedule for the replay window's agent-wide drain, which polls rather than blocks (see
// replay_drain_agent_or_fatal). What it usually waits for is one completion handler that is still
// delivering its records and finishes within tens of microseconds, so the first polls run back to
// back and only then does the wait back off, doubling from `min_sleep` up to `max_sleep`.
class drain_backoff
{
public:
    static constexpr uint32_t spin_polls = 64;
    static constexpr auto     min_sleep  = std::chrono::microseconds{50};
    static constexpr auto     max_sleep  = std::chrono::milliseconds{2};

    // The pause before the next poll; zero means yield and poll again at once.
    std::chrono::nanoseconds next()
    {
        if(m_polls < spin_polls)
        {
            ++m_polls;
            return std::chrono::nanoseconds{0};
        }
        const auto current = m_sleep;
        m_sleep =
            std::min<std::chrono::nanoseconds>(m_sleep * 2, std::chrono::nanoseconds{max_sleep});
        return current;
    }

    void wait()
    {
        const auto pause = next();
        if(pause.count() == 0)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for(pause);
    }

private:
    uint32_t                 m_polls = 0;
    std::chrono::nanoseconds m_sleep = min_sleep;
};
}  // namespace kernel_replay
}  // namespace rocprofiler
