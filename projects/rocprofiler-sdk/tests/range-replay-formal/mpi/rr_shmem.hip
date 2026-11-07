// Device-initiated communication inside a replay range: each PE's kernel atomically adds 1 to a
// counter in its right neighbour's symmetric heap (rocSHMEM, fine-grained device memory, reached
// over IPC). After one execution of the range every PE's counter must be exactly 1.
//
// RR_APP_MODE=packet|host. In host mode the barrier that completes the exchange is inside the
// range; in packet mode it is after it.

#include <rocprofiler-sdk/experimental/range_replay.h>

#include <hip/hip_runtime.h>
#include <mpi.h>
#include <rocshmem/rocshmem.hpp>

#include <dlfcn.h>
#include <setjmp.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace rocshmem;

#define HIP_CHECK(call)                                                                            \
    do                                                                                             \
    {                                                                                              \
        hipError_t _err = (call);                                                                  \
        if(_err != hipSuccess)                                                                     \
        {                                                                                          \
            fprintf(stderr, "HIP error '%s' at %s:%d\n", hipGetErrorString(_err), __FILE__,       \
                    __LINE__);                                                                     \
            MPI_Abort(MPI_COMM_WORLD, 2);                                                          \
        }                                                                                          \
    } while(0)

__global__ void
add_to_neighbour(int* counter, int* local_scratch, int peer)
{
    if(threadIdx.x == 0 && blockIdx.x == 0)
    {
        rocshmem_int_atomic_add(counter, 1, peer);
        rocshmem_quiet();
        local_scratch[0] += 1;
    }
}

int
main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    const char* base = std::getenv("RR_MPI_DEVICE_BASE");
    HIP_CHECK(hipSetDevice((base ? std::atoi(base) : 4) + rank));

    rocshmem_init();
    const int me   = rocshmem_my_pe();
    const int npes = rocshmem_n_pes();
    const int peer = (me + 1) % npes;

    auto* counter = static_cast<int*>(rocshmem_malloc(sizeof(int)));
    int*  scratch = nullptr;
    HIP_CHECK(hipMalloc(&scratch, sizeof(int)));

    const char* mode_env  = std::getenv("RR_APP_MODE");
    const bool  host_mode = mode_env != nullptr && std::string{mode_env} == "host";

    auto* begin_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_begin)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_begin"));
    auto* end_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_end)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_end"));
    using jmpbuf_fn_t = void* (*) ();
    auto* jmp_fn      = reinterpret_cast<jmpbuf_fn_t>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_host_jmpbuf"));

    // Warm-up loads the kernel's code object, then reset.
    add_to_neighbour<<<1, 64>>>(counter, scratch, peer);
    HIP_CHECK(hipDeviceSynchronize());
    rocshmem_barrier_all();
    HIP_CHECK(hipMemset(counter, 0, sizeof(int)));
    HIP_CHECK(hipMemset(scratch, 0, sizeof(int)));
    HIP_CHECK(hipDeviceSynchronize());
    rocshmem_barrier_all();

    volatile auto begin_status = ROCPROFILER_STATUS_SUCCESS;
    if(begin_fn != nullptr)
    {
        if(host_mode)
        {
            if(sigsetjmp(*static_cast<sigjmp_buf*>(jmp_fn()), 1) == 0) begin_status = begin_fn(1);
        }
        else
        {
            begin_status = begin_fn(1);
        }
    }
    add_to_neighbour<<<1, 64>>>(counter, scratch, peer);
    HIP_CHECK(hipDeviceSynchronize());
    if(host_mode) rocshmem_barrier_all();
    if(end_fn != nullptr && begin_status == ROCPROFILER_STATUS_SUCCESS) end_fn();
    rocshmem_barrier_all();

    int got = 0, local = 0;
    HIP_CHECK(hipMemcpy(&got, counter, sizeof(int), hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(&local, scratch, sizeof(int), hipMemcpyDeviceToHost));
    const int ok = (got == 1 && local == 1) ? 1 : 0;
    int       all_ok = 0;
    MPI_Allreduce(&ok, &all_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    printf("[pe %d] mode=%s counter_from_neighbour=%d local_scratch=%d %s\n", me,
           host_mode ? "host" : "packet", got, local, ok ? "ok" : "MISMATCH");
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);
    if(rank == 0) printf("[rr-shmem] %s\n", all_ok ? "PASS" : "FAIL");

    rocshmem_free(counter);
    rocshmem_finalize();
    MPI_Finalize();
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
