// Range replay instrumentation resolved at run time, so one binary runs against range replay v1
// (packet only), v2 (packet and host) or no SDK at all.
//
//   RR_APP_MODE  none (default) | packet | host
//   RR_RANGE     kernel (default): data is mapped before the range, which holds only kernels.
//                full: the range also re-uploads the inputs, downloads the result and reduces it
//                on the host.
#pragma once

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <setjmp.h>
#ifndef RTLD_DEFAULT
#    define RTLD_DEFAULT ((void*) 0)
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    RR_MODE_NONE   = 0,
    RR_MODE_PACKET = 1,
    RR_MODE_HOST   = 2
};

typedef struct
{
    int (*begin)(uint64_t);
    int (*end)(void);
    void* (*jmpbuf)(void);
    int (*host_pass)(uint64_t*);
    int (*track)(void*, size_t);
    int resolved;
} rr_api_t;

static inline rr_api_t*
rr_api(void)
{
    static rr_api_t api;
    if(!api.resolved)
    {
        api.begin     = (int (*)(uint64_t)) dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_begin");
        api.end       = (int (*)(void)) dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_end");
        api.jmpbuf    = (void* (*) (void)) dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_host_jmpbuf");
        api.host_pass = (int (*)(uint64_t*)) dlsym(RTLD_DEFAULT,
                                                   "rocprofiler_range_replay_get_host_pass");
        api.track     = (int (*)(void*, size_t)) dlsym(RTLD_DEFAULT,
                                                   "rocprofiler_range_replay_host_track_memory");
        api.resolved  = 1;
    }
    return &api;
}

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
    if(rr_api()->host_pass) rr_api()->host_pass(&p);
    return (unsigned long) p;
}

// Host replay re-runs the range's host code, so host memory the range writes must be registered to
// be restored before each pass. RR_NO_TRACK=1 skips the registration (negative control).
static inline void
rr_track(void* ptr, size_t bytes)
{
    const char* nt = getenv("RR_NO_TRACK");
    if(rr_mode() != RR_MODE_HOST || (nt && nt[0] == '1') || rr_api()->track == NULL) return;
    if(rr_api()->track(ptr, bytes) != 0) fprintf(stderr, "[rr-app] host_track_memory failed\n");
}

// A statement, so the sigsetjmp stays in the caller's frame as ISO C requires.
#define RR_BEGIN(range_id)                                                                         \
    do                                                                                             \
    {                                                                                              \
        const int rr_m_ = rr_mode();                                                               \
        if(rr_m_ != RR_MODE_NONE && rr_api()->begin == NULL)                                       \
        {                                                                                          \
            fprintf(stderr, "[rr-app] range replay API is not in this process\n");                 \
            exit(2);                                                                               \
        }                                                                                          \
        if(rr_m_ == RR_MODE_HOST)                                                                  \
        {                                                                                          \
            if(rr_api()->jmpbuf == NULL)                                                           \
            {                                                                                      \
                fprintf(stderr, "[rr-app] host range replay is not in this SDK\n");               \
                exit(2);                                                                           \
            }                                                                                      \
            if(sigsetjmp(*(sigjmp_buf*) rr_api()->jmpbuf(), 1) == 0) rr_api()->begin(range_id);   \
        }                                                                                          \
        else if(rr_m_ == RR_MODE_PACKET)                                                           \
            rr_api()->begin(range_id);                                                             \
    } while(0)

#define RR_END()                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if(rr_mode() != RR_MODE_NONE) rr_api()->end();                                             \
    } while(0)
