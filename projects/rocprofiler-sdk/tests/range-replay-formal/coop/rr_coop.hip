// A cooperative launch (grid.sync) inside a range. Each replayed pass must see the range-entry
// state and finish -- a pass that can't make all workgroups co-resident would hang in grid.sync.
// RR_APP_MODE=packet|host. The range replay entry points are resolved with dlsym.
#include <rocprofiler-sdk/experimental/range_replay.h>

#include <hip/hip_cooperative_groups.h>
#include <hip/hip_runtime.h>

#include <dlfcn.h>
#include <setjmp.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace cg = cooperative_groups;

#define HIP_CHECK(x)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if(const auto e = (x); e != hipSuccess)                                                    \
        {                                                                                          \
            std::fprintf(stderr, "[app] FAIL: %s at %s:%d\n", hipGetErrorString(e), __FILE__,     \
                         __LINE__);                                                                \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while(0)

__global__ void
coop_rotate(int* data, int* out, int n, int rounds)
{
    auto       grid = cg::this_grid();
    const auto tid  = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    for(int r = 0; r < rounds; ++r)
    {
        if(tid < n) data[tid] += 1;
        grid.sync();
        if(tid < n) out[tid] = data[(tid + 1) % n];
        grid.sync();
        if(tid < n) data[tid] = out[tid];
        grid.sync();
    }
}

int
main()
{
    const auto* mode = std::getenv("RR_APP_MODE");
    bool        host = mode != nullptr && std::string{mode} == "host";
    auto*       begin_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_begin)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_begin"));
    auto* end_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_end)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_end"));
    using jmpbuf_fn_t = void* (*) ();
    auto* jmp_fn      = reinterpret_cast<jmpbuf_fn_t>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_host_jmpbuf"));
    const bool no_range = std::getenv("RR_COOP_NO_RANGE") != nullptr;
    if(no_range)
    {
        begin_fn = [](uint64_t) { return ROCPROFILER_STATUS_SUCCESS; };
        end_fn   = [] { return ROCPROFILER_STATUS_SUCCESS; };
        host     = false;
    }
    if(!begin_fn || !end_fn || (host && !jmp_fn))
    {
        std::fprintf(stderr, "[app] FAIL: range replay API is not in this process\n");
        return 1;
    }

    int dev = 0, coop = 0;
    HIP_CHECK(hipGetDevice(&dev));
    HIP_CHECK(hipDeviceGetAttribute(&coop, hipDeviceAttributeCooperativeLaunch, dev));
    if(!coop)
    {
        std::printf("[app] SKIP: no cooperative launch\n");
        return 0;
    }
    int per_sm = 0, sms = 0;
    constexpr int block = 256;
    HIP_CHECK(hipOccupancyMaxActiveBlocksPerMultiprocessor(&per_sm, coop_rotate, block, 0));
    HIP_CHECK(hipDeviceGetAttribute(&sms, hipDeviceAttributeMultiprocessorCount, dev));
    const int grid   = per_sm * sms;
    const int n      = grid * block;
    const int rounds = 8;

    std::vector<int> h(n);
    for(int i = 0; i < n; ++i)
        h[i] = i;
    int *d_data = nullptr, *d_out = nullptr;
    HIP_CHECK(hipMalloc(&d_data, n * sizeof(int)));
    HIP_CHECK(hipMalloc(&d_out, n * sizeof(int)));

    int   nn = n, rr = rounds;
    void* args[] = {&d_data, &d_out, &nn, &rr};
    // Warm-up so the code object is loaded outside the range.
    HIP_CHECK(hipMemcpy(d_data, h.data(), n * sizeof(int), hipMemcpyHostToDevice));
    HIP_CHECK(hipLaunchCooperativeKernel(reinterpret_cast<void*>(coop_rotate), dim3(grid),
                                         dim3(block), args, 0, nullptr));
    HIP_CHECK(hipDeviceSynchronize());
    HIP_CHECK(hipMemcpy(d_data, h.data(), n * sizeof(int), hipMemcpyHostToDevice));
    HIP_CHECK(hipDeviceSynchronize());

    volatile auto st = ROCPROFILER_STATUS_SUCCESS;
    if(host)
    {
        if(sigsetjmp(*static_cast<sigjmp_buf*>(jmp_fn()), 1) == 0) st = begin_fn(1);
    }
    else
        st = begin_fn(1);
    if(st != ROCPROFILER_STATUS_SUCCESS)
    {
        std::fprintf(stderr, "[app] FAIL: begin %d\n", static_cast<int>(st));
        return 1;
    }
    HIP_CHECK(hipLaunchCooperativeKernel(reinterpret_cast<void*>(coop_rotate), dim3(grid),
                                         dim3(block), args, 0, nullptr));
    HIP_CHECK(hipDeviceSynchronize());
    if(end_fn() != ROCPROFILER_STATUS_SUCCESS)
    {
        std::fprintf(stderr, "[app] FAIL: end\n");
        return 1;
    }

    std::vector<int> got(n);
    HIP_CHECK(hipMemcpy(got.data(), d_data, n * sizeof(int), hipMemcpyDeviceToHost));
    // One execution: each round adds 1 then rotates left by one.
    std::vector<int> expect = h;
    for(int r = 0; r < rounds; ++r)
    {
        for(auto& v : expect)
            v += 1;
        std::vector<int> nxt(n);
        for(int i = 0; i < n; ++i)
            nxt[i] = expect[(i + 1) % n];
        expect = nxt;
    }
    int bad = 0;
    for(int i = 0; i < n; ++i)
        bad += got[i] != expect[i];
    std::printf("[app] mode=%s grid=%d block=%d mismatches=%d\n", host ? "host" : "packet", grid,
                block, bad);
    std::printf(bad ? "[app] FAIL\n" : "[app] PASS\n");
    return bad ? 1 : 0;
}
