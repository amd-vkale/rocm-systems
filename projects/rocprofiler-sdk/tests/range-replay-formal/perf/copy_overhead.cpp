// Per-copy cost of hsa_amd_memory_async_copy host->device, which range replay's copy wrapper
// tracks whenever a tool has configured range replay (even outside any range).
//   copy_overhead <bytes> <iters> <mode: latency|stream>
// latency: submit one copy, wait for it, repeat.  stream: submit all, wait for the last.
#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if((x) != HSA_STATUS_SUCCESS)                                                              \
        {                                                                                          \
            std::fprintf(stderr, "%s failed\n", #x);                                               \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while(0)

namespace
{
hsa_agent_t      g_gpu{}, g_cpu{};
hsa_amd_memory_pool_t g_gpu_pool{}, g_cpu_pool{};

hsa_status_t
find_agents(hsa_agent_t a, void*)
{
    hsa_device_type_t t;
    hsa_agent_get_info(a, HSA_AGENT_INFO_DEVICE, &t);
    if(t == HSA_DEVICE_TYPE_GPU && g_gpu.handle == 0) g_gpu = a;
    if(t == HSA_DEVICE_TYPE_CPU && g_cpu.handle == 0) g_cpu = a;
    return HSA_STATUS_SUCCESS;
}

hsa_status_t
find_pool(hsa_amd_memory_pool_t p, void* data)
{
    hsa_amd_segment_t seg;
    hsa_amd_memory_pool_get_info(p, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &seg);
    bool alloc = false;
    hsa_amd_memory_pool_get_info(p, HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_ALLOWED, &alloc);
    auto* out = static_cast<hsa_amd_memory_pool_t*>(data);
    if(seg == HSA_AMD_SEGMENT_GLOBAL && alloc && out->handle == 0) *out = p;
    return HSA_STATUS_SUCCESS;
}
}  // namespace

int
main(int argc, char** argv)
{
    const size_t bytes = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 4096;
    const int    iters = argc > 2 ? std::atoi(argv[2]) : 20000;
    const auto   mode  = std::string{argc > 3 ? argv[3] : "latency"};

    CHECK(hsa_init());
    CHECK(hsa_iterate_agents(find_agents, nullptr));
    CHECK(hsa_amd_agent_iterate_memory_pools(g_gpu, find_pool, &g_gpu_pool));
    CHECK(hsa_amd_agent_iterate_memory_pools(g_cpu, find_pool, &g_cpu_pool));

    void* src = nullptr;
    void* dst = nullptr;
    CHECK(hsa_amd_memory_pool_allocate(g_cpu_pool, bytes, 0, &src));
    CHECK(hsa_amd_memory_pool_allocate(g_gpu_pool, bytes, 0, &dst));
    CHECK(hsa_amd_agents_allow_access(1, &g_gpu, nullptr, src));
    std::memset(src, 1, bytes);

    hsa_signal_t sig;
    CHECK(hsa_signal_create(1, 0, nullptr, &sig));

    auto run = [&](int n) {
        const auto t0 = std::chrono::steady_clock::now();
        if(mode == "latency")
        {
            for(int i = 0; i < n; ++i)
            {
                hsa_signal_store_relaxed(sig, 1);
                CHECK(hsa_amd_memory_async_copy(dst, g_gpu, src, g_cpu, bytes, 0, nullptr, sig));
                while(hsa_signal_wait_scacquire(sig, HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX,
                                                HSA_WAIT_STATE_ACTIVE) != 0)
                {}
            }
        }
        else
        {
            hsa_signal_store_relaxed(sig, n);
            for(int i = 0; i < n; ++i)
                CHECK(hsa_amd_memory_async_copy(dst, g_gpu, src, g_cpu, bytes, 0, nullptr, sig));
            while(hsa_signal_wait_scacquire(sig, HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX,
                                            HSA_WAIT_STATE_ACTIVE) != 0)
            {}
        }
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0)
                   .count() /
               n;
    };

    run(std::min(iters, 500));
    std::vector<double> samples;
    for(int r = 0; r < 5; ++r)
        samples.push_back(run(iters));
    std::sort(samples.begin(), samples.end());
    std::printf("bytes=%zu iters=%d mode=%s median_us_per_copy=%.3f min=%.3f max=%.3f\n", bytes,
                iters, mode.c_str(), samples[2], samples.front(), samples.back());

    hsa_signal_destroy(sig);
    hsa_amd_memory_pool_free(src);
    hsa_amd_memory_pool_free(dst);
    hsa_shut_down();
    return 0;
}
