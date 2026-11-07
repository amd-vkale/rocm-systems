// Fortran bridge: rr_f_range runs `body` inside range `id`. The sigsetjmp host replay needs lives in
// this frame, which stays live while body runs, so a host-replay pass re-enters body from here.
#include "rr_rt.h"

#include <stdint.h>

void
rr_f_range(int64_t id, void (*body)(void))
{
    RR_BEGIN((uint64_t) id);
    body();
    RR_END();
}

int
rr_f_range_is_full(void)
{
    return rr_range_is_full();
}

const char*
rr_f_mode_name(void)
{
    const char* m = getenv("RR_APP_MODE");
    return m ? m : "none";
}

void
rr_f_track(void* ptr, int64_t bytes)
{
    rr_track(ptr, (size_t) bytes);
}
