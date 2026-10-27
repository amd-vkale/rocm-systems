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

#include "lib/common/logging.hpp"
#include "lib/rocprofiler-sdk/code_object/code_object.hpp"
#include "lib/rocprofiler-sdk/hsa/hsa.hpp"

#include <fmt/format.h>
#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>

namespace rocprofiler
{
namespace kernel_replay
{
namespace untracked_memory
{
namespace
{
// Anything below the first page or above the canonical 47-bit user range is a scalar, not an
// address a kernel can dereference.
constexpr uint64_t min_candidate = 0x10000;
constexpr uint64_t max_candidate = 0x00007fffffffffffULL;

// Writable mappings of this process that are ordinary host memory. Device mappings through
// /dev/kfd or /dev/dri are left out: HSA classifies the ones that matter.
std::vector<address_span_t>
read_writable_host_mappings()
{
    auto out  = std::vector<address_span_t>{};
    auto file = std::ifstream{"/proc/self/maps"};
    auto line = std::string{};
    while(std::getline(file, line))
    {
        unsigned long long begin    = 0;
        unsigned long long end      = 0;
        char               perms[8] = {};
        if(std::sscanf(line.c_str(), "%llx-%llx %7s", &begin, &end, perms) != 3) continue;
        if(perms[0] != 'r' || perms[1] != 'w') continue;
        if(line.find("/dev/kfd") != std::string::npos || line.find("/dev/dri") != std::string::npos)
            continue;
        // Thread stacks, vvar, vdso and vsyscall hold scalars and host-only pointers, not memory a
        // kernel is handed as data. A by-value scalar whose bit pattern lands in the stack range
        // would otherwise read as a false hit.
        if(line.find("[stack") != std::string::npos || line.find("[vvar") != std::string::npos ||
           line.find("[vdso") != std::string::npos || line.find("[vsyscall") != std::string::npos)
            continue;
        out.push_back({begin, end});
    }
    std::sort(
        out.begin(), out.end(), [](const auto& l, const auto& r) { return l.begin < r.begin; });
    return out;
}

bool
in_spans(const std::vector<address_span_t>& sorted, uint64_t address)
{
    auto itr = std::upper_bound(
        sorted.begin(), sorted.end(), address, [](uint64_t a, const address_span_t& s) {
            return a < s.begin;
        });
    if(itr == sorted.begin()) return false;
    --itr;
    return address >= itr->begin && address < itr->end;
}

pointer_facts_t::type_t
convert_type(hsa_amd_pointer_type_t type)
{
    switch(type)
    {
        case HSA_EXT_POINTER_TYPE_UNKNOWN: return pointer_facts_t::type_t::unknown;
        case HSA_EXT_POINTER_TYPE_HSA: return pointer_facts_t::type_t::hsa;
        case HSA_EXT_POINTER_TYPE_LOCKED: return pointer_facts_t::type_t::locked;
        case HSA_EXT_POINTER_TYPE_RESERVED_ADDR: return pointer_facts_t::type_t::reserved;
        case HSA_EXT_POINTER_TYPE_HSA_VMEM: return pointer_facts_t::type_t::vmem;
        default: return pointer_facts_t::type_t::other;
    }
}

bool
default_copy(void* dst, const void* src, size_t size)
{
    auto* core = hsa::get_core_table();
    return core != nullptr && core->hsa_memory_copy_fn != nullptr &&
           core->hsa_memory_copy_fn(dst, src, size) == HSA_STATUS_SUCCESS;
}

// NOLINTNEXTLINE(performance-no-int-to-ptr)
void*
as_pointer(uint64_t address)
{
    return reinterpret_cast<void*>(address);
}

// Name of a loaded kernel, if the code object registered one.
std::optional<const std::string*>
kernel_name(uint64_t kernel_object)
{
    auto found = std::optional<const std::string*>{};
    code_object::iterate_loaded_code_objects([&](const code_object::hsa::code_object& co) {
        if(found) return;
        for(const auto& symbol : co.symbols)
        {
            if(symbol && symbol->rocp_data.kernel_object == kernel_object)
            {
                found = symbol->name;
                return;
            }
        }
    });
    return found;
}

// Size of the explicit plus hidden kernel arguments of a loaded kernel.
std::optional<size_t>
kernarg_segment_size(uint64_t kernel_object)
{
    auto found = std::optional<size_t>{};
    code_object::iterate_loaded_code_objects([&](const code_object::hsa::code_object& co) {
        if(found) return;
        for(const auto& symbol : co.symbols)
        {
            if(symbol && symbol->rocp_data.kernel_object == kernel_object)
            {
                found = symbol->rocp_data.kernarg_segment_size;
                return;
            }
        }
    });
    return found;
}
}  // namespace

const char*
to_string(kind_t kind)
{
    switch(kind)
    {
        case kind_t::host_pinned: return "host-pinned memory (hipHostMalloc)";
        case kind_t::managed: return "managed memory (hipMallocManaged)";
        case kind_t::virtual_mem: return "a virtual-memory allocation (hipMallocAsync, hipMemMap)";
        case kind_t::device_other:
            return "device memory the snapshot does not track (fine-grained, uncached, peer)";
        case kind_t::locked_host: return "locked host memory (hipHostRegister)";
        case kind_t::host_pageable: return "pageable host memory";
    }
    return "untracked memory";
}

pointer_facts_fn_t
make_hsa_pointer_facts()
{
    auto mappings = std::make_shared<std::optional<std::vector<address_span_t>>>();

    return [mappings](uint64_t address) {
        auto facts = pointer_facts_t{};

        auto* ext  = hsa::get_amd_ext_table();
        auto* core = hsa::get_core_table();
        if(ext != nullptr && ext->hsa_amd_pointer_info_fn != nullptr)
        {
            auto info = hsa_amd_pointer_info_t{};
            info.size = sizeof(info);
            if(ext->hsa_amd_pointer_info_fn(
                   as_pointer(address), &info, nullptr, nullptr, nullptr) == HSA_STATUS_SUCCESS)
            {
                facts.type = convert_type(info.type);
                facts.fine_grained =
                    (info.global_flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED) != 0;
                facts.agent_base = reinterpret_cast<uint64_t>(info.agentBaseAddress);
                facts.size       = info.sizeInBytes;
                facts.owner      = info.agentOwner.handle;
                if(facts.type == pointer_facts_t::type_t::hsa && info.agentOwner.handle != 0 &&
                   core != nullptr && core->hsa_agent_get_info_fn != nullptr)
                {
                    auto device = hsa_device_type_t{};
                    if(core->hsa_agent_get_info_fn(
                           info.agentOwner, HSA_AGENT_INFO_DEVICE, &device) == HSA_STATUS_SUCCESS)
                        facts.owner_is_cpu = (device == HSA_DEVICE_TYPE_CPU);
                }
            }
        }

        // HSA reports neither pageable host memory nor hipHostRegister'd memory with an owner or
        // extent, so fall back to the process's writable mappings. This is not gated on XNACK:
        // hipHostRegister maps memory into the GPU address space even with XNACK off, so a kernel
        // can write it. Stack, vvar, vdso and vsyscall are excluded because a by-value scalar's
        // bit pattern can land in them.
        if(facts.type == pointer_facts_t::type_t::unknown)
        {
            if(!mappings->has_value()) *mappings = read_writable_host_mappings();
            facts.in_writable_host_mapping = in_spans(**mappings, address);
        }
        return facts;
    };
}

std::vector<address_span_t>
covered_spans(const memory_snapshot::device_snapshot_t& snapshot)
{
    auto out = std::vector<address_span_t>{};
    out.reserve(snapshot.blocks.size());
    for(const auto& block : snapshot.blocks)
    {
        const auto begin = reinterpret_cast<uint64_t>(block.gpu_addr);
        out.push_back({begin, begin + block.host_copy.size()});
    }
    std::sort(
        out.begin(), out.end(), [](const auto& l, const auto& r) { return l.begin < r.begin; });
    return out;
}

std::optional<kind_t>
classify_pointer(const pointer_facts_t& facts)
{
    using type_t = pointer_facts_t::type_t;
    switch(facts.type)
    {
        case type_t::hsa: return facts.owner_is_cpu ? kind_t::host_pinned : kind_t::device_other;
        case type_t::reserved: return kind_t::managed;
        case type_t::vmem: return kind_t::virtual_mem;
        case type_t::locked: return kind_t::locked_host;
        case type_t::other: return kind_t::device_other;
        case type_t::unknown:
            if(facts.in_writable_host_mapping) return kind_t::host_pageable;
            return std::nullopt;
    }
    return std::nullopt;
}

region_t
region_of(kind_t kind, const pointer_facts_t& facts)
{
    auto out = region_t{};
    out.kind = kind;
    if(kind == kind_t::device_other && facts.type == pointer_facts_t::type_t::hsa &&
       facts.fine_grained && facts.agent_base != 0 && facts.size != 0)
    {
        out.base  = facts.agent_base;
        out.size  = facts.size;
        out.owner = facts.owner;
    }
    return out;
}

std::vector<hit_t>
scan_kernarg(const uint8_t*                     kernarg,
             size_t                             size,
             const std::vector<address_span_t>& covered,
             const pointer_facts_fn_t&          facts,
             std::unordered_set<uint64_t>&      seen)
{
    auto hits = std::vector<hit_t>{};
    if(kernarg == nullptr) return hits;

    const auto limit = size >= implicit_arg_block_bytes ? size - implicit_arg_block_bytes : size;
    for(size_t off = 0; off + sizeof(uint64_t) <= limit; off += sizeof(uint64_t))
    {
        auto value = uint64_t{0};
        std::memcpy(&value, kernarg + off, sizeof(value));
        if(value < min_candidate || value > max_candidate) continue;
        if(in_spans(covered, value)) continue;
        if(!seen.insert(value).second) continue;

        const auto f = facts(value);
        if(const auto kind = classify_pointer(f); kind)
            hits.push_back(hit_t{off, value, region_of(*kind, f)});
    }
    return hits;
}

region_set_t::region_set_t()
: m_copy{default_copy}
{}

region_set_t::region_set_t(copy_fn_t copy)
: m_copy{std::move(copy)}
{}

region_set_t::save_status_t
region_set_t::save(const region_t& region)
{
    if(region.size == 0) return save_status_t::not_restorable;
    if(contains(region.base)) return save_status_t::already_saved;
    if(region.size > max_total_bytes - m_bytes) return save_status_t::too_large;

    auto saved   = saved_t{};
    saved.region = region;
    try
    {
        saved.data.resize(region.size);
    } catch(const std::bad_alloc&)
    {
        return save_status_t::copy_failed;
    }
    if(!m_copy(saved.data.data(), as_pointer(region.base), region.size))
        return save_status_t::copy_failed;

    try
    {
        m_saved.emplace_back(std::move(saved));
    } catch(const std::bad_alloc&)
    {
        return save_status_t::copy_failed;
    }
    m_bytes += region.size;
    return save_status_t::saved;
}

bool
region_set_t::contains(uint64_t address) const
{
    return std::any_of(m_saved.begin(), m_saved.end(), [address](const saved_t& s) {
        return address >= s.region.base && address < s.region.base + s.region.size;
    });
}

bool
region_set_t::restore() const
{
    return std::all_of(m_saved.begin(), m_saved.end(), [this](const saved_t& s) {
        return m_copy(as_pointer(s.region.base), s.data.data(), s.data.size());
    });
}

bool
save_dispatch_memory(region_set_t&                             saved,
                     const uint8_t*                            kernarg,
                     size_t                                    size,
                     const memory_snapshot::device_snapshot_t& snapshot,
                     hsa_agent_t                               agent,
                     const pointer_facts_fn_t&                 facts)
{
    auto seen = std::unordered_set<uint64_t>{};
    for(const auto& hit : scan_kernarg(kernarg, size, covered_spans(snapshot), facts, seen))
    {
        const auto  where   = fmt::format("kernel replay: kernarg offset {} points at 0x{:x}, {}",
                                       hit.kernarg_offset,
                                       hit.address,
                                       to_string(hit.region.kind));
        const bool  foreign = hit.region.size != 0 && hit.region.owner != agent.handle;
        const char* why     = nullptr;
        switch(foreign ? region_set_t::save_status_t::not_restorable : saved.save(hit.region))
        {
            case region_set_t::save_status_t::saved:
                ROCP_INFO << fmt::format(
                    "{}; saved {} bytes at 0x{:x}", where, hit.region.size, hit.region.base);
                continue;
            case region_set_t::save_status_t::already_saved: continue;
            case region_set_t::save_status_t::not_restorable:
                if(foreign)
                    why = "it belongs to another agent";
                else if(hit.region.kind == kind_t::virtual_mem ||
                        hit.region.kind == kind_t::host_pageable)
                    why = "its extent is not known";
                else
                    why = "it is not device memory of this agent";
                break;
            case region_set_t::save_status_t::too_large:
                why = "saving it would exceed the per-dispatch limit";
                break;
            case region_set_t::save_status_t::copy_failed: why = "copying it failed"; break;
        }
        LOG_FIRST_N(WARNING, 1) << fmt::format(
            "{}, which the snapshot does not restore and {}; running this dispatch once without "
            "replay",
            where,
            why);
        return false;
    }
    return true;
}

bool
save_dispatch_memory(region_set_t&                             saved,
                     const hsa_kernel_dispatch_packet_t&       packet,
                     const memory_snapshot::device_snapshot_t& snapshot,
                     hsa_agent_t                               agent)
{
    // HIP's runtime-internal copy and fill kernels (__amd_rocclr_copyBuffer,
    // __amd_rocclr_fillBufferAligned, __amd_rocclr_initHeap, ...) are handed HIP's own device
    // scratch buffers and pinned staging memory, not application data. Treating those as untracked
    // memory would run every hipMemcpy / hipMemset dispatch once without replay; a copy or fill
    // replayed N times is idempotent within one kernel replay, so replaying them is safe. The
    // application's own kernels still take the strict path below.
    const auto name = kernel_name(packet.kernel_object);
    if(name && *name && (*name)->rfind("__amd_rocclr_", 0) == 0) return true;

    const auto size = kernarg_segment_size(packet.kernel_object);
    if(!size)
    {
        LOG_FIRST_N(WARNING, 1) << "kernel replay: the argument size of the dispatched kernel is "
                                   "not known; running this dispatch once without replay";
        return false;
    }
    return save_dispatch_memory(saved,
                                static_cast<const uint8_t*>(packet.kernarg_address),
                                *size,
                                snapshot,
                                agent,
                                make_hsa_pointer_facts());
}
}  // namespace untracked_memory
}  // namespace kernel_replay
}  // namespace rocprofiler
