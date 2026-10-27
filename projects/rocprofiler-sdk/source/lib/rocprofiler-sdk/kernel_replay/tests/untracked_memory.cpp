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
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "lib/rocprofiler-sdk/kernel_replay/untracked_memory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace msnp = ::rocprofiler::kernel_replay::memory_snapshot;
namespace um   = ::rocprofiler::kernel_replay::untracked_memory;

namespace
{
using type_t = um::pointer_facts_t::type_t;

constexpr uint64_t tracked_addr = 0x7f0000100000;
constexpr uint64_t vmem_addr    = 0x7f0000200000;

um::pointer_facts_t
facts_of(type_t type, bool owner_is_cpu = false, bool in_host_mapping = false)
{
    auto f                     = um::pointer_facts_t{};
    f.type                     = type;
    f.owner_is_cpu             = owner_is_cpu;
    f.in_writable_host_mapping = in_host_mapping;
    return f;
}

uint64_t
address_of(const std::vector<uint8_t>& buf)
{
    return reinterpret_cast<uint64_t>(buf.data());
}

constexpr auto this_agent  = hsa_agent_t{9};
constexpr auto other_agent = hsa_agent_t{10};

// Fine-grained device memory of `owner` for `buf`, a virtual-memory mapping at vmem_addr, unknown
// to HSA otherwise. Host-pinned instead of device memory when `pinned`.
um::pointer_facts_fn_t
facts_for(const std::vector<uint8_t>& buf, hsa_agent_t owner = this_agent, bool pinned = false)
{
    return [&buf, owner, pinned](uint64_t address) {
        const auto base = address_of(buf);
        if(address >= base && address < base + buf.size())
        {
            auto f         = facts_of(type_t::hsa, pinned);
            f.fine_grained = true;
            f.agent_base   = base;
            f.size         = buf.size();
            f.owner        = owner.handle;
            return f;
        }
        if(address == vmem_addr) return facts_of(type_t::vmem);
        return facts_of(type_t::unknown);
    };
}

um::region_set_t
host_copy_set(bool ok = true)
{
    return um::region_set_t{[ok](void* dst, const void* src, size_t size) {
        if(ok) std::memcpy(dst, src, size);
        return ok;
    }};
}

std::vector<uint8_t>
kernarg_with(const std::vector<uint64_t>& words, size_t trailing = 0)
{
    auto out = std::vector<uint8_t>(words.size() * sizeof(uint64_t) + trailing, 0);
    std::memcpy(out.data(), words.data(), words.size() * sizeof(uint64_t));
    return out;
}

bool
save(um::region_set_t& saved, const std::vector<uint64_t>& words, const um::pointer_facts_fn_t& f)
{
    const auto kernarg = kernarg_with(words);
    return um::save_dispatch_memory(
        saved, kernarg.data(), kernarg.size(), msnp::device_snapshot_t{}, this_agent, f);
}
}  // namespace

TEST(kernel_replay_untracked_memory, classifies_memory_the_snapshot_does_not_restore)
{
    EXPECT_EQ(um::classify_pointer(facts_of(type_t::hsa, true)), um::kind_t::host_pinned);
    EXPECT_EQ(um::classify_pointer(facts_of(type_t::hsa, false)), um::kind_t::device_other);
    EXPECT_EQ(um::classify_pointer(facts_of(type_t::reserved)), um::kind_t::managed);
    EXPECT_EQ(um::classify_pointer(facts_of(type_t::vmem)), um::kind_t::virtual_mem);
    EXPECT_EQ(um::classify_pointer(facts_of(type_t::locked)), um::kind_t::locked_host);
    EXPECT_EQ(um::classify_pointer(facts_of(type_t::unknown, false, true)),
              um::kind_t::host_pageable);
    EXPECT_EQ(um::classify_pointer(facts_of(type_t::unknown)), std::nullopt)
        << "not memory a kernel can reach";
}

TEST(kernel_replay_untracked_memory, ignores_scalars_covered_memory_and_hidden_arguments)
{
    const auto covered = std::vector<um::address_span_t>{{tracked_addr, tracked_addr + 4096}};
    const auto facts   = [](uint64_t) { return facts_of(type_t::hsa, true); };

    // A small scalar, then a pointer into the snapshot, then the hidden block with a stale pointer.
    auto kernarg = kernarg_with({42, tracked_addr + 8}, um::implicit_arg_block_bytes);
    std::memcpy(kernarg.data() + 2 * sizeof(uint64_t), &vmem_addr, sizeof(vmem_addr));

    auto seen = std::unordered_set<uint64_t>{};
    EXPECT_TRUE(um::scan_kernarg(kernarg.data(), kernarg.size(), covered, facts, seen).empty());
}

TEST(kernel_replay_untracked_memory, fine_grained_memory_of_this_agent_is_restored_between_passes)
{
    auto buf   = std::vector<uint8_t>(64, 1);
    auto saved = host_copy_set();

    ASSERT_TRUE(save(saved, {42, address_of(buf) + 16, address_of(buf)}, facts_for(buf)));
    ASSERT_EQ(saved.size(), 1u) << "one allocation, however many arguments point into it";
    EXPECT_EQ(saved.bytes(), buf.size());

    // A pass writes 2; the next pass must start from 1 again.
    std::fill(buf.begin(), buf.end(), 2);
    ASSERT_TRUE(saved.restore());
    EXPECT_EQ(buf, std::vector<uint8_t>(64, 1));
}

TEST(kernel_replay_untracked_memory, memory_that_cannot_be_saved_runs_the_dispatch_once)
{
    auto buf = std::vector<uint8_t>(64, 1);

    const auto runs_once = [](const um::pointer_facts_fn_t& facts, uint64_t address) {
        auto saved = host_copy_set();
        return !save(saved, {address}, facts) && saved.empty();
    };
    EXPECT_TRUE(runs_once(facts_for(buf, this_agent, true), address_of(buf))) << "host-pinned";
    EXPECT_TRUE(runs_once(facts_for(buf, other_agent), address_of(buf))) << "another agent's";
    EXPECT_TRUE(runs_once(facts_for(buf), vmem_addr)) << "extent unknown";

    auto failing = host_copy_set(false);
    EXPECT_FALSE(save(failing, {address_of(buf)}, facts_for(buf))) << "copy failed";
    EXPECT_TRUE(failing.empty());
}
