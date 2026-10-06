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

// Kernel replay with PC sampling. The kernel launched with the replay block size is replayed
// KR_PCS_PASSES times; PC sampling stays configured for the whole run. With KR_PCS_PASS >= 0 the
// tool locally starts the PC sampling context on that pass only and stops it on every other one,
// so the replayed dispatch must collect about as many samples as the reference launch of the same
// kernel that is not replayed (KR_PCS_EXPECT=one). With KR_PCS_PASS=-1 every pass samples and the
// replayed dispatch collects about one reference's worth per pass (KR_PCS_EXPECT=all). Either way,
// no sample may be left without a dispatch: the replay window's own blits run on the
// application's queue and must not be sampled.

#include <rocprofiler-sdk/buffer.h>
#include <rocprofiler-sdk/callback_tracing.h>
#include <rocprofiler-sdk/experimental/kernel_replay.h>
#include <rocprofiler-sdk/internal_threading.h>
#include <rocprofiler-sdk/pc_sampling.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define RC(call)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        auto _st = (call);                                                                         \
        if(_st != ROCPROFILER_STATUS_SUCCESS)                                                      \
        {                                                                                          \
            fprintf(stderr, "[kr-pcs] FAIL %s: %s\n", #call, rocprofiler_get_status_string(_st));  \
            std::abort();                                                                          \
        }                                                                                          \
    } while(0)

namespace
{
constexpr uint32_t kReplayBlock    = 192;
constexpr uint32_t kReferenceBlock = 256;

long
env_long(const char* name, long fallback)
{
    const char* value = std::getenv(name);
    return value ? std::strtol(value, nullptr, 10) : fallback;
}

const uint64_t    g_passes = static_cast<uint64_t>(env_long("KR_PCS_PASSES", 4));
const long        g_pass   = env_long("KR_PCS_PASS", 0);
const std::string g_expect = std::getenv("KR_PCS_EXPECT") ? std::getenv("KR_PCS_EXPECT") : "one";

rocprofiler_context_id_t g_replay_ctx{0};
rocprofiler_context_id_t g_pcs_ctx{0};
rocprofiler_buffer_id_t  g_pcs_buffer{0};
bool                     g_pcs_available = false;

std::atomic<uint64_t> g_target_dispatch{0};
std::atomic<uint64_t> g_reference_dispatch{0};
std::atomic<uint64_t> g_target_samples{0};
std::atomic<uint64_t> g_reference_samples{0};
std::atomic<uint64_t> g_unattributed_samples{0};
std::atomic<uint64_t> g_other_samples{0};
std::atomic<uint64_t> g_passes_run{0};

uint64_t replay_pass_count(rocprofiler_kernel_dispatch_info_t, rocprofiler_user_data_t)
{
    return g_passes;
}

void
kernel_replay_cb(rocprofiler_callback_tracing_record_t record, rocprofiler_user_data_t*, void*)
{
    if(record.kind != ROCPROFILER_CALLBACK_TRACING_KERNEL_REPLAY) return;
    auto*      p = static_cast<rocprofiler_callback_tracing_kernel_replay_data_t*>(record.payload);
    const auto block    = p->dispatch_info.workgroup_size.x;
    const auto dispatch = p->dispatch_info.dispatch_id;

    if(record.operation == ROCPROFILER_KERNEL_REPLAY_CONFIG &&
       record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER)
    {
        if(block == kReplayBlock)
        {
            g_target_dispatch.store(dispatch);
            p->replay_pass_count = replay_pass_count;
        }
        else if(block == kReferenceBlock)
        {
            g_reference_dispatch.store(dispatch);
        }
        return;
    }

    if(record.operation != ROCPROFILER_KERNEL_REPLAY_PASS ||
       record.phase != ROCPROFILER_CALLBACK_PHASE_ENTER || block != kReplayBlock)
        return;

    g_passes_run.fetch_add(1);
    if(g_pass < 0 || !g_pcs_available) return;
    if(static_cast<long>(p->current_pass) == g_pass)
        RC(p->replay_start_context(g_pcs_ctx));
    else
        RC(p->replay_stop_context(g_pcs_ctx));
}

void
count_sample(uint64_t dispatch)
{
    if(dispatch == 0)
        g_unattributed_samples.fetch_add(1);
    else if(dispatch == g_target_dispatch.load())
        g_target_samples.fetch_add(1);
    else if(dispatch == g_reference_dispatch.load())
        g_reference_samples.fetch_add(1);
    else
        g_other_samples.fetch_add(1);
}

void
pcs_buffer_cb(rocprofiler_context_id_t,
              rocprofiler_buffer_id_t,
              rocprofiler_record_header_t** headers,
              size_t                        num_headers,
              void*,
              uint64_t)
{
    for(size_t i = 0; i < num_headers; ++i)
    {
        const auto* h = headers[i];
        if(!h || h->category != ROCPROFILER_BUFFER_CATEGORY_PC_SAMPLING) continue;
        if(h->kind == ROCPROFILER_PC_SAMPLING_RECORD_HOST_TRAP_V0_SAMPLE)
            count_sample(
                static_cast<const rocprofiler_pc_sampling_record_host_trap_v0_t*>(h->payload)
                    ->dispatch_id);
        else if(h->kind == ROCPROFILER_PC_SAMPLING_RECORD_STOCHASTIC_V0_SAMPLE)
            count_sample(
                static_cast<const rocprofiler_pc_sampling_record_stochastic_v0_t*>(h->payload)
                    ->dispatch_id);
    }
}

std::vector<rocprofiler_agent_id_t>
gpu_agents()
{
    auto out = std::vector<rocprofiler_agent_id_t>{};
    RC(rocprofiler_query_available_agents(
        ROCPROFILER_AGENT_INFO_VERSION_0,
        [](rocprofiler_agent_version_t, const void** agents, size_t count, void* data) {
            auto* ids = static_cast<std::vector<rocprofiler_agent_id_t>*>(data);
            for(size_t i = 0; i < count; ++i)
            {
                const auto* agent = static_cast<const rocprofiler_agent_v0_t*>(agents[i]);
                if(agent->type == ROCPROFILER_AGENT_TYPE_GPU) ids->push_back(agent->id);
            }
            return ROCPROFILER_STATUS_SUCCESS;
        },
        sizeof(rocprofiler_agent_v0_t),
        &out));
    return out;
}

bool
configure_pcs()
{
    RC(rocprofiler_create_context(&g_pcs_ctx));
    RC(rocprofiler_create_buffer(g_pcs_ctx,
                                 1 << 20,
                                 1 << 19,
                                 ROCPROFILER_BUFFER_POLICY_LOSSLESS,
                                 pcs_buffer_cb,
                                 nullptr,
                                 &g_pcs_buffer));
    auto thread = rocprofiler_callback_thread_t{};
    RC(rocprofiler_create_callback_thread(&thread));
    RC(rocprofiler_assign_callback_thread(g_pcs_buffer, thread));

    bool any = false;
    for(auto id : gpu_agents())
    {
        auto configs = std::vector<rocprofiler_pc_sampling_configuration_t>{};
        auto status  = rocprofiler_query_pc_sampling_agent_configurations(
            id,
            [](const rocprofiler_pc_sampling_configuration_t* cfgs, size_t n, void* data) {
                auto* v = static_cast<std::vector<rocprofiler_pc_sampling_configuration_t>*>(data);
                v->insert(v->end(), cfgs, cfgs + n);
                return ROCPROFILER_STATUS_SUCCESS;
            },
            &configs);
        if(status != ROCPROFILER_STATUS_SUCCESS || configs.empty()) continue;

        // Prefer host trap, the method every MI300 configuration offers.
        auto chosen = configs.front();
        for(const auto& cfg : configs)
            if(cfg.method == ROCPROFILER_PC_SAMPLING_METHOD_HOST_TRAP) chosen = cfg;
        const auto interval = std::max<uint64_t>(chosen.min_interval, 1);
        if(rocprofiler_configure_pc_sampling_service(
               g_pcs_ctx, id, chosen.method, chosen.unit, interval, g_pcs_buffer, 0) ==
           ROCPROFILER_STATUS_SUCCESS)
            any = true;
    }
    if(any) RC(rocprofiler_start_context(g_pcs_ctx));
    return any;
}

int
tool_init(rocprofiler_client_finalize_t, void*)
{
    RC(rocprofiler_create_context(&g_replay_ctx));
    RC(rocprofiler_configure_callback_tracing_service(g_replay_ctx,
                                                      ROCPROFILER_CALLBACK_TRACING_KERNEL_REPLAY,
                                                      nullptr,
                                                      0,
                                                      kernel_replay_cb,
                                                      nullptr));
    g_pcs_available = configure_pcs();
    if(!g_pcs_available) fprintf(stderr, "[kr-pcs] PC sampling unavailable\n");
    RC(rocprofiler_start_context(g_replay_ctx));
    return 0;
}

void
tool_fini(void*)
{
    if(!g_pcs_available)
    {
        fprintf(stdout, "[kr-pcs] PC sampling unavailable\n");
        return;
    }
    rocprofiler_flush_buffer(g_pcs_buffer);

    const auto target       = g_target_samples.load();
    const auto reference    = g_reference_samples.load();
    const auto ratio        = reference ? static_cast<double>(target) / reference : 0.0;
    const auto passes       = g_passes_run.load();
    const auto unattributed = g_unattributed_samples.load();

    fprintf(stdout,
            "[kr-pcs] passes=%lu sampled_pass=%ld target=%lu reference=%lu ratio=%.2f "
            "unattributed=%lu other=%lu\n",
            static_cast<unsigned long>(passes),
            g_pass,
            static_cast<unsigned long>(target),
            static_cast<unsigned long>(reference),
            ratio,
            static_cast<unsigned long>(unattributed),
            static_cast<unsigned long>(g_other_samples.load()));

    auto fail = [](const char* why) { fprintf(stdout, "[kr-pcs] FAIL: %s\n", why); };
    if(passes != g_passes) return fail("the replayed dispatch did not run every pass");
    if(reference < 200) return fail("too few reference samples to compare");
    if(unattributed != 0) return fail("samples without a dispatch (a replay blit was sampled)");

    // One execution's worth when a single pass samples; one per pass when all of them do. The
    // bounds are wide because sampling is statistical, but far apart from each other.
    const auto expected = (g_expect == "all") ? static_cast<double>(g_passes) : 1.0;
    if(ratio < 0.5 * expected || ratio > 1.6 * expected)
        return fail("the replayed dispatch's samples do not match the passes that sampled");
    fprintf(stdout, "[kr-pcs] PASS\n");
}
}  // namespace

extern "C" rocprofiler_tool_configure_result_t*
rocprofiler_configure(uint32_t, const char*, uint32_t priority, rocprofiler_client_id_t* id)
{
    if(priority > 0) return nullptr;
    id->name        = "kernel-replay-pc-sampling";
    static auto cfg = rocprofiler_tool_configure_result_t{
        sizeof(rocprofiler_tool_configure_result_t), &tool_init, &tool_fini, nullptr};
    return &cfg;
}
