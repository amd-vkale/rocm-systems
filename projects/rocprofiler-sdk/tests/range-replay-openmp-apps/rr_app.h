// Range replay instrumentation shared by the XSBench / RSBench OpenMP offload ports.
//
//   RR_APP_MODE  none (default) | packet | host
//   RR_RANGE     kernel (default): data is mapped with `target enter data` before the range, so
//                the range holds only the `target teams` kernel.
//                full: the range wraps the whole simulation call, including the map clauses'
//                allocations and copies and the host-side hash reduction.
#pragma once

#include <rocprofiler-sdk/experimental/range_replay.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    RR_MODE_NONE   = 0,
    RR_MODE_PACKET = 1,
    RR_MODE_HOST   = 2
};

static inline int
rr_mode(void)
{
    const char* m = getenv("RR_APP_MODE");
    if(m == NULL) return RR_MODE_NONE;
    if(strcmp(m, "packet") == 0) return RR_MODE_PACKET;
    if(strcmp(m, "host") == 0) return RR_MODE_HOST;
    return RR_MODE_NONE;
}

static inline int
rr_range_is_full(void)
{
    const char* r = getenv("RR_RANGE");
    return r != NULL && strcmp(r, "full") == 0;
}

static inline unsigned long
rr_host_pass(void)
{
    uint64_t p = 0;
    rocprofiler_range_replay_get_host_pass(&p);
    return (unsigned long) p;
}

// A statement, so the sigsetjmp stays in the caller's frame as ISO C requires.
#define RR_BEGIN(range_id, enabled)                                                                \
    do                                                                                             \
    {                                                                                              \
        if(enabled)                                                                                \
        {                                                                                          \
            rocprofiler_status_t rr_status_ = ROCPROFILER_STATUS_SUCCESS;                          \
            if(rr_mode() == RR_MODE_HOST)                                                          \
            {                                                                                      \
                ROCPROFILER_RANGE_REPLAY_HOST_BEGIN(range_id, rr_status_);                         \
            }                                                                                      \
            else if(rr_mode() == RR_MODE_PACKET)                                                   \
            {                                                                                      \
                rr_status_ = rocprofiler_range_replay_begin(range_id);                             \
            }                                                                                      \
            if(rr_status_ != ROCPROFILER_STATUS_SUCCESS)                                           \
                fprintf(stderr, "[rr-app] begin failed: %d\n", (int) rr_status_);                  \
        }                                                                                          \
    } while(0)

#define RR_END(enabled)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if((enabled) && rr_mode() != RR_MODE_NONE) rocprofiler_range_replay_end();                 \
    } while(0)
