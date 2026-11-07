// Another thread's device writes must survive a range replay on the same agent.
//
// The main thread replays ranges over its own buffer. A second thread meanwhile writes a new value
// into its own device buffer with hipMemcpyAsync (SDMA for a pinned source), waits for the copy,
// and reads a few words back. A replay window that restores the whole agent's tracked memory
// after that copy landed hands the second thread an older value: a lost write.
//
// RR_APP_MODE=packet|host  RR_RANGES=<n>  RR_WRITER_MB=<n>  RR_RANGE_MB=<n>
// RR_WRITER_SYNC=free|end: write continuously, or once per range just after end() is called, so
// the write lands in the replay window instead of declining the range.

#include <rocprofiler-sdk/experimental/range_replay.h>

#include <hip/hip_runtime.h>

#include <dlfcn.h>
#include <setjmp.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#define HIP_OK(call)                                                                               \
    do                                                                                             \
    {                                                                                              \
        hipError_t _err = (call);                                                                  \
        if(_err != hipSuccess)                                                                     \
        {                                                                                          \
            fprintf(stderr, "HIP error '%s' at %s:%d\n", hipGetErrorString(_err), __FILE__,       \
                    __LINE__);                                                                     \
            std::exit(EXIT_FAILURE);                                                               \
        }                                                                                          \
    } while(0)

namespace
{
__global__ void
scale(float* x, size_t n, float s)
{
    const size_t i = blockIdx.x * static_cast<size_t>(blockDim.x) + threadIdx.x;
    if(i < n) x[i] = x[i] * s + 1.0f;
}

size_t
env_size(const char* name, size_t fallback)
{
    const char* v = std::getenv(name);
    return v ? std::strtoull(v, nullptr, 10) : fallback;
}

std::atomic<bool>     g_stop{false};
std::atomic<uint64_t> g_end_calls{0};
bool                  g_sync_end = false;
std::atomic<uint64_t> g_writes{0};
std::atomic<uint64_t> g_lost{0};

void
writer(size_t bytes)
{
    HIP_OK(hipSetDevice(0));
    const size_t words = bytes / sizeof(uint32_t);
    uint32_t*    d     = nullptr;
    uint32_t*    h     = nullptr;
    uint32_t*    back  = nullptr;
    HIP_OK(hipMalloc(&d, bytes));
    HIP_OK(hipHostMalloc(&h, bytes, hipHostMallocDefault));
    HIP_OK(hipHostMalloc(&back, 3 * sizeof(uint32_t), hipHostMallocDefault));
    hipStream_t s;
    HIP_OK(hipStreamCreateWithFlags(&s, hipStreamNonBlocking));

    uint64_t seen_end = 0;
    for(uint32_t v = 1; !g_stop.load(); ++v)
    {
        if(g_sync_end && v > 3)
        {
            while(!g_stop.load() && g_end_calls.load() == seen_end)
                std::this_thread::yield();
            if(g_stop.load()) break;
            seen_end = g_end_calls.load();
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        for(size_t i = 0; i < words; i += 1024)
            h[i] = v;
        h[words - 1] = v;
        HIP_OK(hipMemcpyAsync(d, h, bytes, hipMemcpyHostToDevice, s));
        HIP_OK(hipStreamSynchronize(s));
        // Give a concurrent replay window time to run its exit restore over this buffer.
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        HIP_OK(hipMemcpyAsync(&back[0], d, sizeof(uint32_t), hipMemcpyDeviceToHost, s));
        HIP_OK(hipMemcpyAsync(&back[1], d + (words / 2 / 1024) * 1024, sizeof(uint32_t),
                              hipMemcpyDeviceToHost, s));
        HIP_OK(hipMemcpyAsync(&back[2], d + words - 1, sizeof(uint32_t), hipMemcpyDeviceToHost, s));
        HIP_OK(hipStreamSynchronize(s));
        g_writes.fetch_add(1);
        if(back[0] != v || back[1] != v || back[2] != v)
        {
            if(g_lost.fetch_add(1) < 5)
                fprintf(stderr, "[writer] LOST WRITE: wrote %u, read back %u %u %u\n", v, back[0],
                        back[1], back[2]);
        }
    }
    HIP_OK(hipStreamDestroy(s));
    HIP_OK(hipFree(d));
    HIP_OK(hipHostFree(h));
    HIP_OK(hipHostFree(back));
}
}  // namespace

int
main()
{
    const char* mode_env = std::getenv("RR_APP_MODE");
    const bool  host     = mode_env != nullptr && std::string{mode_env} == "host";
    const auto  ranges   = env_size("RR_RANGES", 200);
    const auto  writer_b = env_size("RR_WRITER_MB", 32) << 20;
    const auto  range_b  = env_size("RR_RANGE_MB", 256) << 20;
    const char* sync_env = std::getenv("RR_WRITER_SYNC");
    g_sync_end           = sync_env != nullptr && std::string{sync_env} == "end";

    auto* begin_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_begin)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_begin"));
    auto* end_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_end)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_end"));
    auto* jmp_fn = reinterpret_cast<void* (*) ()>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_host_jmpbuf"));
    if(!begin_fn || !end_fn || (host && !jmp_fn))
    {
        fprintf(stderr, "[app] FAIL: range replay API is not in this process\n");
        return EXIT_FAILURE;
    }

    HIP_OK(hipSetDevice(0));
    const size_t n = range_b / sizeof(float);
    float*       x = nullptr;
    HIP_OK(hipMalloc(&x, range_b));
    HIP_OK(hipMemset(x, 0, range_b));
    scale<<<(n + 255) / 256, 256>>>(x, n, 1.0f);
    HIP_OK(hipDeviceSynchronize());

    std::thread w{writer, writer_b};
    while(g_writes.load() < 3)
        std::this_thread::yield();

    for(uint64_t r = 0; r < ranges; ++r)
    {
        volatile int st = 0;
        if(host)
        {
            if(sigsetjmp(*static_cast<sigjmp_buf*>(jmp_fn()), 1) == 0) st = begin_fn(r + 1);
        }
        else
        {
            st = begin_fn(r + 1);
        }
        if(st != ROCPROFILER_STATUS_SUCCESS)
        {
            fprintf(stderr, "[app] FAIL: begin returned %d\n", st);
            return EXIT_FAILURE;
        }
        scale<<<(n + 255) / 256, 256>>>(x, n, 0.5f);
        HIP_OK(hipDeviceSynchronize());
        g_end_calls.fetch_add(1);
        end_fn();
    }

    g_stop.store(true);
    w.join();
    printf("[app] mode=%s ranges=%zu writer_writes=%lu lost_writes=%lu\n", host ? "host" : "packet",
           static_cast<size_t>(ranges), g_writes.load(), g_lost.load());
    if(g_lost.load() != 0)
    {
        printf("[app] FAIL\n");
        return EXIT_FAILURE;
    }
    printf("[app] PASS\n");
    return EXIT_SUCCESS;
}
