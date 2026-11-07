// Range replay instrumentation shared by the XSBench / RSBench OpenMP offload ports.
//
//   RR_APP_MODE  none (default) | packet | host
//   RR_RANGE     kernel (default): data is mapped with `target enter data` before the range, so
//                the range holds only the `target teams` kernel.
//                full: the range wraps the whole simulation call, including the map clauses'
//                allocations and copies and the host-side hash reduction.
//
// The API is resolved at run time (see rr_rt.h), so one binary runs against range replay v1
// (packet only), v2 (packet and host) or no SDK.
#pragma once

#include "rr_rt.h"

#define RR_APP_BEGIN(range_id, enabled)                                                            \
    do                                                                                             \
    {                                                                                              \
        if(enabled) RR_BEGIN(range_id);                                                            \
    } while(0)

#define RR_APP_END(enabled)                                                                        \
    do                                                                                             \
    {                                                                                              \
        if(enabled) RR_END();                                                                      \
    } while(0)
