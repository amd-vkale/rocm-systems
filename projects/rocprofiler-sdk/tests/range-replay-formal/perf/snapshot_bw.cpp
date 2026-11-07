// Device->host and host->device bandwidth of the copy primitives a range snapshot could use:
//   pageable : std::vector destination, hsa_memory_copy (what memory_snapshot does today)
//   pinned   : hsa_amd_memory_pool_allocate'd system memory, hsa_memory_copy
//   locked   : std::vector registered with hsa_amd_memory_lock, hsa_memory_copy on the agent ptr
//   async    : pinned destination, hsa_amd_memory_async_copy
//   snapshot_bw <MiB>
#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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
hsa_agent_t           g_gpu{}, g_cpu{};
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

double
gbps(size_t bytes, const std::function<void()>& fn)
{
    fn();
    auto best = 1e30;
    for(int i = 0; i < 3; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        best = std::min(
            best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    return bytes / best / 1e9;
}
}  // namespace

int
main(int argc, char** argv)
{
    const size_t bytes = (argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 256) << 20;
    CHECK(hsa_init());
    CHECK(hsa_iterate_agents(find_agents, nullptr));
    CHECK(hsa_amd_agent_iterate_memory_pools(g_gpu, find_pool, &g_gpu_pool));
    CHECK(hsa_amd_agent_iterate_memory_pools(g_cpu, find_pool, &g_cpu_pool));

    void* dev = nullptr;
    CHECK(hsa_amd_memory_pool_allocate(g_gpu_pool, bytes, 0, &dev));

    std::vector<unsigned char> pageable(bytes, 1);
    void*                      pinned = nullptr;
    CHECK(hsa_amd_memory_pool_allocate(g_cpu_pool, bytes, 0, &pinned));
    CHECK(hsa_amd_agents_allow_access(1, &g_gpu, nullptr, pinned));
    std::memset(pinned, 1, bytes);

    std::vector<unsigned char> lockable(bytes, 1);
    void*                      locked_agent_ptr = nullptr;
    CHECK(hsa_amd_memory_lock(lockable.data(), bytes, &g_gpu, 1, &locked_agent_ptr));

    hsa_signal_t sig;
    CHECK(hsa_signal_create(1, 0, nullptr, &sig));
    auto async = [&](void* dst, hsa_agent_t da, const void* src, hsa_agent_t sa) {
        hsa_signal_store_relaxed(sig, 1);
        CHECK(hsa_amd_memory_async_copy(dst, da, src, sa, bytes, 0, nullptr, sig));
        while(hsa_signal_wait_scacquire(sig, HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX,
                                        HSA_WAIT_STATE_BLOCKED) != 0)
        {}
    };

    std::printf("bytes=%zu MiB\n", bytes >> 20);
    std::printf("vector resize (zero-fill)  %7.2f GB/s\n", gbps(bytes, [&] {
                    std::vector<unsigned char> v;
                    v.resize(bytes);
                }));
    std::printf("D2H pageable hsa_memory_copy %7.2f GB/s\n",
                gbps(bytes, [&] { CHECK(hsa_memory_copy(pageable.data(), dev, bytes)); }));
    std::printf("H2D pageable hsa_memory_copy %7.2f GB/s\n",
                gbps(bytes, [&] { CHECK(hsa_memory_copy(dev, pageable.data(), bytes)); }));
    std::printf("D2H pinned   hsa_memory_copy %7.2f GB/s\n",
                gbps(bytes, [&] { CHECK(hsa_memory_copy(pinned, dev, bytes)); }));
    std::printf("H2D pinned   hsa_memory_copy %7.2f GB/s\n",
                gbps(bytes, [&] { CHECK(hsa_memory_copy(dev, pinned, bytes)); }));
    std::printf("D2H locked   hsa_memory_copy %7.2f GB/s\n",
                gbps(bytes, [&] { CHECK(hsa_memory_copy(locked_agent_ptr, dev, bytes)); }));
    std::printf("D2H pinned   async_copy      %7.2f GB/s\n",
                gbps(bytes, [&] { async(pinned, g_cpu, dev, g_gpu); }));
    std::printf("H2D pinned   async_copy      %7.2f GB/s\n",
                gbps(bytes, [&] { async(dev, g_gpu, pinned, g_cpu); }));
    std::printf("lock+unlock vector           %7.2f GB/s\n", gbps(bytes, [&] {
                    void* p = nullptr;
                    std::vector<unsigned char> v(bytes);
                    CHECK(hsa_amd_memory_lock(v.data(), bytes, &g_gpu, 1, &p));
                    CHECK(hsa_amd_memory_unlock(v.data()));
                }));

    hsa_amd_memory_unlock(lockable.data());
    hsa_signal_destroy(sig);
    hsa_amd_memory_pool_free(pinned);
    hsa_amd_memory_pool_free(dev);
    hsa_shut_down();
    return 0;
}
