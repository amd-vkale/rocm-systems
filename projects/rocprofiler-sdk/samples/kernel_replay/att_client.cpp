// MIT License
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include "client.hpp"

#include <rocprofiler-sdk/experimental/kernel_replay.h>
#include <rocprofiler-sdk/experimental/thread_trace.h>
#include <rocprofiler-sdk/registration.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace
{
constexpr uint64_t kPasses = 2;

rocprofiler_context_id_t g_replay_ctx{0};
rocprofiler_context_id_t g_att_ctx{0};
rocprofiler_kernel_id_t  g_target_kernel = UINT64_MAX;

std::atomic<int> g_att_records{0};

uint64_t replay_pass_count(rocprofiler_kernel_dispatch_info_t, rocprofiler_user_data_t)
{
    return kPasses;
}

void
kernel_replay_cb(rocprofiler_callback_tracing_record_t record, rocprofiler_user_data_t*, void*)
{
    if(record.kind != ROCPROFILER_CALLBACK_TRACING_KERNEL_REPLAY) return;
    auto* p = static_cast<rocprofiler_callback_tracing_kernel_replay_data_t*>(record.payload);
    if(p->dispatch_info.workgroup_size.x != kReplayBlockX) return;

    if(record.operation == ROCPROFILER_KERNEL_REPLAY_CONFIG &&
       record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER)
    {
        p->replay_pass_count = replay_pass_count;
        g_target_kernel      = p->dispatch_info.kernel_id;
    }
}

rocprofiler_thread_trace_control_flags_t
att_dispatch_cb(rocprofiler_agent_id_t,
                rocprofiler_queue_id_t,
                rocprofiler_async_correlation_id_t,
                rocprofiler_kernel_id_t kernel_id,
                rocprofiler_dispatch_id_t,
                void*,
                rocprofiler_user_data_t*)
{
    return (kernel_id == g_target_kernel) ? ROCPROFILER_THREAD_TRACE_CONTROL_START_AND_STOP
                                          : ROCPROFILER_THREAD_TRACE_CONTROL_NONE;
}

void att_shader_cb(rocprofiler_thread_trace_shader_data_t, rocprofiler_user_data_t)
{
    g_att_records.fetch_add(1);
}

int
tool_init(rocprofiler_client_finalize_t, void*)
{
    KR_CHECK(rocprofiler_create_context(&g_replay_ctx));
    KR_CHECK(
        rocprofiler_configure_callback_tracing_service(g_replay_ctx,
                                                       ROCPROFILER_CALLBACK_TRACING_KERNEL_REPLAY,
                                                       nullptr,
                                                       0,
                                                       kernel_replay_cb,
                                                       nullptr));

    KR_CHECK(rocprofiler_create_context(&g_att_ctx));
    bool any_att = false;
    for(auto agent : gpu_agents())
    {
        auto parameters = std::vector<rocprofiler_thread_trace_parameter_t>{
            {ROCPROFILER_THREAD_TRACE_PARAMETER_SHADER_ENGINE_MASK, 0x1}};
        any_att |= rocprofiler_configure_dispatch_thread_trace_service(g_att_ctx,
                                                                       agent,
                                                                       parameters.data(),
                                                                       parameters.size(),
                                                                       att_dispatch_cb,
                                                                       att_shader_cb,
                                                                       nullptr) ==
                   ROCPROFILER_STATUS_SUCCESS;
    }
    if(!any_att)
    {
        fprintf(stderr, "ATT unavailable\n");
        return -1;
    }
    KR_CHECK(rocprofiler_start_context(g_att_ctx));
    KR_CHECK(rocprofiler_start_context(g_replay_ctx));
    return 0;
}

void
tool_fini(void*)
{
    // Thread trace runs on every replay pass, so each pass delivers at least one shader-data
    // record.
    fprintf(stderr,
            "[att] att_records=%d (expected at least %lu)\n",
            g_att_records.load(),
            static_cast<unsigned long>(kPasses));
    if(g_att_records.load() < static_cast<int>(kPasses)) std::abort();
}
}  // namespace

extern "C" rocprofiler_tool_configure_result_t*
rocprofiler_configure(uint32_t, const char*, uint32_t priority, rocprofiler_client_id_t* id)
{
    if(priority > 0) return nullptr;
    id->name        = "kernel-replay-att";
    static auto cfg = rocprofiler_tool_configure_result_t{
        sizeof(rocprofiler_tool_configure_result_t), &tool_init, &tool_fini, nullptr};
    return &cfg;
}
