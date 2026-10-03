// MIT License
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

// A tool that opens replay ranges from ROCTx instead of asking the application to call the range
// API. The application (RR_APP_MODE=roctx) only pushes and pops ROCTx ranges, the way annotated
// codes and the Kokkos Tools connector already do. This client opens a replay range when a ROCTx
// range named kRoctxRangeName is pushed and closes it when that same range is popped.
//
// Both range replay and roctxRangePush/Pop are scoped to the calling thread, so the mapping is
// direct. roctxRangePop carries no name, so the client keeps its own per-thread stack to know
// whether a pop closes the range it opened or one nested inside it. Ranges do not nest, so only
// the outermost occurrence of the name opens one. Process-wide ROCTx ranges (roctxRangeStart /
// Stop) can start and stop on different threads and cannot delimit a replay range.

#include "client.hpp"

#include <rocprofiler-sdk/callback_tracing.h>
#include <rocprofiler-sdk/marker/api_args.h>
#include <rocprofiler-sdk/marker/api_id.h>
#include <rocprofiler-sdk/registration.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
constexpr uint64_t kPasses = 4;

rocprofiler_context_id_t g_ctx{0};

// One entry per open ROCTx range on this thread: whether its push opened the replay range.
thread_local std::vector<bool> tl_stack{};
thread_local bool              tl_open = false;

std::atomic<int>      g_opened{0};
std::atomic<int>      g_begin_failures{0};
std::atomic<int>      g_passes{0};
std::atomic<int>      g_closes{0};
std::atomic<uint64_t> g_dispatch_count{0};
std::atomic<int>      g_status{ROCPROFILER_RANGE_REPLAY_STATUS_LAST};
std::atomic<uint64_t> g_divergence{0};

uint64_t
pass_count(uint64_t, rocprofiler_user_data_t)
{
    return kPasses;
}

void
range_replay_cb(rocprofiler_callback_tracing_record_t record, rocprofiler_user_data_t*, void*)
{
    if(record.kind != ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY) return;

    auto* data = static_cast<rocprofiler_callback_tracing_range_replay_data_t*>(record.payload);
    if(data->range_id != kRangeId) return;

    if(record.operation == ROCPROFILER_RANGE_REPLAY_CONFIG &&
       record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER)
        data->pass_count_cb = pass_count;
    else if(record.operation == ROCPROFILER_RANGE_REPLAY_PASS &&
            record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER)
        g_passes.fetch_add(1);
    else if(record.operation == ROCPROFILER_RANGE_REPLAY_CLOSE &&
            record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER)
    {
        fprintf(stderr,
                "[roctx] close status=%s dispatches=%lu divergence=%lu\n",
                status_name(data->status),
                static_cast<unsigned long>(data->dispatch_count),
                static_cast<unsigned long>(data->divergence_count));
        g_closes.fetch_add(1);
        g_status.store(data->status);
        g_dispatch_count.store(data->dispatch_count);
        g_divergence.store(data->divergence_count);
    }
}

void
marker_cb(rocprofiler_callback_tracing_record_t record, rocprofiler_user_data_t*, void*)
{
    if(record.kind != ROCPROFILER_CALLBACK_TRACING_MARKER_CORE_API) return;
    auto* data = static_cast<rocprofiler_callback_tracing_marker_api_data_t*>(record.payload);

    // Open after the push has returned and close before the pop unwinds, so the replay window
    // falls inside the ROCTx range it belongs to.
    if(record.operation == ROCPROFILER_MARKER_CORE_API_ID_roctxRangePushA &&
       record.phase == ROCPROFILER_CALLBACK_PHASE_EXIT)
    {
        const char* msg   = data->args.roctxRangePushA.message;
        const bool  opens = !tl_open && msg != nullptr && std::strcmp(msg, kRoctxRangeName) == 0;
        if(opens)
        {
            if(rocprofiler_range_replay_begin(kRangeId) == ROCPROFILER_STATUS_SUCCESS)
            {
                tl_open = true;
                g_opened.fetch_add(1);
            }
            else
                g_begin_failures.fetch_add(1);
        }
        tl_stack.push_back(opens && tl_open);
    }
    else if(record.operation == ROCPROFILER_MARKER_CORE_API_ID_roctxRangePop &&
            record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER)
    {
        if(tl_stack.empty()) return;
        const bool closes = tl_stack.back();
        tl_stack.pop_back();
        if(closes)
        {
            RR_CHECK(rocprofiler_range_replay_end());
            tl_open = false;
        }
    }
}

int
tool_init(rocprofiler_client_finalize_t, void*)
{
    rocprofiler_tracing_operation_t ops[] = {ROCPROFILER_MARKER_CORE_API_ID_roctxRangePushA,
                                             ROCPROFILER_MARKER_CORE_API_ID_roctxRangePop};

    RR_CHECK(rocprofiler_create_context(&g_ctx));
    RR_CHECK(rocprofiler_configure_callback_tracing_service(
        g_ctx, ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY, nullptr, 0, range_replay_cb, nullptr));
    RR_CHECK(rocprofiler_configure_callback_tracing_service(
        g_ctx, ROCPROFILER_CALLBACK_TRACING_MARKER_CORE_API, ops, 2, marker_cb, nullptr));
    RR_CHECK(rocprofiler_start_context(g_ctx));
    return 0;
}

void
tool_fini(void*)
{
    fprintf(stderr,
            "[roctx] opened=%d passes=%d closes=%d status=%s\n",
            g_opened.load(),
            g_passes.load(),
            g_closes.load(),
            status_name(static_cast<rocprofiler_range_replay_status_t>(g_status.load())));

    bool ok = true;
    // Exactly one replay range, from the outer ROCTx range; the nested one opened nothing.
    ok = ok && g_opened.load() == 1;
    ok = ok && g_begin_failures.load() == 0;
    ok = ok && g_closes.load() == 1;
    ok = ok && g_passes.load() == static_cast<int>(kPasses - 1);
    ok = ok && g_status.load() == ROCPROFILER_RANGE_REPLAY_STATUS_REPLAYED;
    // All three dispatches were recorded: the nested pop did not close the range early.
    ok = ok && g_dispatch_count.load() == kRangeDispatches;
    // Run with ROCPROF_RANGE_REPLAY_VERIFY=1: the final replayed pass reproduced the application's
    // own execution of the range.
    ok = ok && g_divergence.load() == 0;

    if(!ok)
    {
        fprintf(stderr, "[roctx] FAIL\n");
        std::abort();
    }
}
}  // namespace

extern "C" rocprofiler_tool_configure_result_t*
rocprofiler_configure(uint32_t, const char*, uint32_t priority, rocprofiler_client_id_t* id)
{
    if(priority > 0) return nullptr;
    id->name        = "range-replay-roctx";
    static auto cfg = rocprofiler_tool_configure_result_t{
        sizeof(rocprofiler_tool_configure_result_t), &tool_init, &tool_fini, nullptr};
    return &cfg;
}
