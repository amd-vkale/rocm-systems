// Range replay across MPI ranks.
//
// Each rank drives one GPU (device 4 + local rank by default, RR_MPI_DEVICE_BASE) and opens one
// range around a communication step. RR_MPI_CASE picks what the range holds:
//
//   compute   Jacobi sweeps and a matmul accumulation; the MPI_Allreduce of the checksum runs
//             after the range, so the range itself has no communication.
//   host      the kernels, a device-to-host copy, a host MPI_Allreduce, a host-to-device copy of
//             the reduced value, and a kernel that consumes it.
//   rccl      the kernels and an ncclAllReduce on device buffers: device-initiated peer writes
//             over xGMI, with the kernel spinning on peer flags.
//   rccl-after the same, with the ncclAllReduce right after the range: a compute range in a
//             process whose RCCL communicator maps its peers' memory.
//
// RR_APP_MODE=packet|host opens the range for packet or host replay. RR_MPI_DECLINE_RANK=<r> makes
// rank r decline while the others are still eligible: in host mode it does not arm the jump buffer,
// in packet mode it adds an async host-to-device copy to the range.
//
// After the range every rank checks its result against a host reference of one execution of the
// range, then the ranks agree on pass/fail with an MPI_Allreduce.

#include <rocprofiler-sdk/experimental/range_replay.h>

#include <hip/hip_runtime.h>
#include <mpi.h>
#include <rccl/rccl.h>

#include <dlfcn.h>
#include <setjmp.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define HIP_CHECK(call)                                                                            \
    do                                                                                             \
    {                                                                                              \
        hipError_t _err = (call);                                                                  \
        if(_err != hipSuccess)                                                                     \
        {                                                                                          \
            fprintf(stderr,                                                                        \
                    "[rank %d] HIP error '%s' at %s:%d\n",                                         \
                    g_rank,                                                                        \
                    hipGetErrorString(_err),                                                      \
                    __FILE__,                                                                      \
                    __LINE__);                                                                     \
            MPI_Abort(MPI_COMM_WORLD, 2);                                                          \
        }                                                                                          \
    } while(0)

#define NCCL_CHECK(call)                                                                           \
    do                                                                                             \
    {                                                                                              \
        ncclResult_t _res = (call);                                                                \
        if(_res != ncclSuccess)                                                                    \
        {                                                                                          \
            fprintf(stderr,                                                                        \
                    "[rank %d] RCCL error '%s' at %s:%d\n",                                        \
                    g_rank,                                                                        \
                    ncclGetErrorString(_res),                                                     \
                    __FILE__,                                                                      \
                    __LINE__);                                                                     \
            MPI_Abort(MPI_COMM_WORLD, 2);                                                          \
        }                                                                                          \
    } while(0)

namespace
{
int g_rank = 0;
int g_size = 1;

constexpr int kN           = 64;
constexpr int kCells       = kN * kN;
constexpr int kJacobiIters = 4;

__global__ void
jacobi(const float* u, float* unew, int n)
{
    const int j = blockIdx.x * blockDim.x + threadIdx.x;
    const int i = blockIdx.y * blockDim.y + threadIdx.y;
    if(i >= n || j >= n) return;
    const int p = i * n + j;
    if(i == 0 || j == 0 || i == n - 1 || j == n - 1)
    {
        unew[p] = u[p];
        return;
    }
    unew[p] = 0.25f * (u[p - n] + u[p + n] + u[p - 1] + u[p + 1]);
}

__global__ void
matmul_accum(const float* a, const float* b, float* c, int n)
{
    const int col = blockIdx.x * blockDim.x + threadIdx.x;
    const int row = blockIdx.y * blockDim.y + threadIdx.y;
    if(row >= n || col >= n) return;
    float sum = 0.f;
    for(int k = 0; k < n; ++k)
        sum += a[row * n + k] * b[k * n + col];
    c[row * n + col] += sum;
}

__global__ void
add_scalar(float* c, const float* s, int count)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < count) c[i] += s[0];
}

__global__ void
row_sums(const float* c, float* out, int n)
{
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if(row >= n) return;
    float sum = 0.f;
    for(int k = 0; k < n; ++k)
        sum += c[row * n + k];
    out[row] = sum;
}

void
jacobi_cpu(std::vector<float>& u, int n, int iters)
{
    std::vector<float> tmp(u.size());
    for(int iter = 0; iter < iters; ++iter)
    {
        for(int i = 0; i < n; ++i)
            for(int j = 0; j < n; ++j)
            {
                const int p = i * n + j;
                tmp[p] = (i == 0 || j == 0 || i == n - 1 || j == n - 1)
                             ? u[p]
                             : 0.25f * (u[p - n] + u[p + n] + u[p - 1] + u[p + 1]);
            }
        u.swap(tmp);
    }
}

void
matmul_cpu(const std::vector<float>& a, const std::vector<float>& b, std::vector<float>& c, int n)
{
    for(int row = 0; row < n; ++row)
        for(int col = 0; col < n; ++col)
        {
            float sum = 0.f;
            for(int k = 0; k < n; ++k)
                sum += a[row * n + k] * b[k * n + col];
            c[row * n + col] += sum;
        }
}

std::vector<float>
initial_a(int rank)
{
    std::vector<float> a(kCells);
    for(int i = 0; i < kCells; ++i)
        a[i] = 0.01f * static_cast<float>((i * 17 + rank * 5) % 50);
    return a;
}

std::vector<float>
initial_b()
{
    std::vector<float> b(kCells);
    for(int i = 0; i < kCells; ++i)
        b[i] = 0.01f * static_cast<float>((i * 13) % 40);
    return b;
}

// One execution of the range on the host, for this rank: Jacobi on A, C = 1 + A' * B, and for
// the communicating cases the rank-summed row sums (RCCL) or the rank-summed total (host) added in.
std::vector<float>
reference(const std::string& which, int rank, int size)
{
    const auto b = initial_b();
    auto       c_for = [&](int r) {
        auto a = initial_a(r);
        jacobi_cpu(a, kN, kJacobiIters);
        std::vector<float> c(kCells, 1.f);
        matmul_cpu(a, b, c, kN);
        return c;
    };
    auto c = c_for(rank);
    if(which == "compute") return c;

    if(which == "host")
    {
        double total = 0.0;
        for(int r = 0; r < size; ++r)
        {
            const auto cr = c_for(r);
            for(float v : cr)
                total += v;
        }
        for(float& v : c)
            v += static_cast<float>(total);
        return c;
    }

    std::vector<float> rows(kN, 0.f);
    for(int r = 0; r < size; ++r)
    {
        const auto cr = c_for(r);
        for(int row = 0; row < kN; ++row)
        {
            float sum = 0.f;
            for(int k = 0; k < kN; ++k)
                sum += cr[row * kN + k];
            rows[row] += sum;
        }
    }
    c.resize(kCells + kN);
    std::copy(rows.begin(), rows.end(), c.begin() + kCells);
    return c;
}

int
env_int(const char* name, int fallback)
{
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' ? std::atoi(v) : fallback;
}
}  // namespace

int
main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &g_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &g_size);

    const std::string which = std::getenv("RR_MPI_CASE") ? std::getenv("RR_MPI_CASE") : "compute";
    const char*       mode_env     = std::getenv("RR_APP_MODE");
    const bool        host_mode    = mode_env != nullptr && std::string{mode_env} == "host";
    const int         decline_rank = env_int("RR_MPI_DECLINE_RANK", -1);
    const bool        declines     = decline_rank == g_rank;
    const int         device       = env_int("RR_MPI_DEVICE_BASE", 4) + g_rank;

    HIP_CHECK(hipSetDevice(device));

    auto* begin_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_begin)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_begin"));
    auto* end_fn = reinterpret_cast<decltype(&rocprofiler_range_replay_end)>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_end"));
    using jmpbuf_fn_t = void* (*) ();
    auto* jmp_fn      = reinterpret_cast<jmpbuf_fn_t>(
        dlsym(RTLD_DEFAULT, "rocprofiler_range_replay_host_jmpbuf"));
    if(begin_fn == nullptr || end_fn == nullptr || jmp_fn == nullptr)
    {
        fprintf(stderr, "[rank %d] FAIL: range replay API is not in this process\n", g_rank);
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

    ncclComm_t comm = nullptr;
    const bool uses_rccl = which == "rccl" || which == "rccl-after";
    if(uses_rccl)
    {
        ncclUniqueId id;
        if(g_rank == 0) NCCL_CHECK(ncclGetUniqueId(&id));
        MPI_Bcast(&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD);
        NCCL_CHECK(ncclCommInitRank(&comm, g_size, id, g_rank));
    }

    hipStream_t stream;
    HIP_CHECK(hipStreamCreate(&stream));

    const auto h_a = initial_a(g_rank);
    const auto h_b = initial_b();
    const std::vector<float> h_c(kCells, 1.f);

    float *d_a, *d_tmp, *d_b, *d_c, *d_rows, *d_rows_sum, *d_scalar, *d_extra;
    HIP_CHECK(hipMalloc(&d_a, kCells * sizeof(float)));
    HIP_CHECK(hipMalloc(&d_tmp, kCells * sizeof(float)));
    HIP_CHECK(hipMalloc(&d_b, kCells * sizeof(float)));
    HIP_CHECK(hipMalloc(&d_c, kCells * sizeof(float)));
    HIP_CHECK(hipMalloc(&d_rows, kN * sizeof(float)));
    HIP_CHECK(hipMalloc(&d_rows_sum, kN * sizeof(float)));
    HIP_CHECK(hipMalloc(&d_scalar, sizeof(float)));
    HIP_CHECK(hipMalloc(&d_extra, sizeof(float)));
    float* h_pinned = nullptr;
    HIP_CHECK(hipHostMalloc(&h_pinned, kCells * sizeof(float)));

    const dim3 block2d{16, 16};
    const dim3 grid2d{kN / 16, kN / 16};

    auto reset = [&]() {
        HIP_CHECK(hipMemcpy(d_a, h_a.data(), kCells * sizeof(float), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(d_b, h_b.data(), kCells * sizeof(float), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(d_c, h_c.data(), kCells * sizeof(float), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemset(d_rows_sum, 0, kN * sizeof(float)));
        HIP_CHECK(hipDeviceSynchronize());
    };

    auto allreduce_rows = [&]() {
        row_sums<<<1, kN, 0, stream>>>(d_c, d_rows, kN);
        HIP_CHECK(hipGetLastError());
        NCCL_CHECK(ncclAllReduce(d_rows, d_rows_sum, kN, ncclFloat, ncclSum, comm, stream));
        HIP_CHECK(hipStreamSynchronize(stream));
    };

    // The range body. Every rank runs it identically except for the deliberate decline.
    auto body = [&]() {
        for(int iter = 0; iter < kJacobiIters; ++iter)
        {
            const bool to_tmp = (iter % 2) == 0;
            jacobi<<<grid2d, block2d, 0, stream>>>(to_tmp ? d_a : d_tmp, to_tmp ? d_tmp : d_a, kN);
        }
        matmul_accum<<<grid2d, block2d, 0, stream>>>(d_a, d_b, d_c, kN);
        HIP_CHECK(hipGetLastError());

        if(declines && !host_mode)
        {
            const float one = 1.f;
            HIP_CHECK(hipMemcpyAsync(d_extra, &one, sizeof(float), hipMemcpyHostToDevice, stream));
        }

        if(which == "host")
        {
            HIP_CHECK(hipMemcpyAsync(
                h_pinned, d_c, kCells * sizeof(float), hipMemcpyDeviceToHost, stream));
            HIP_CHECK(hipStreamSynchronize(stream));
            double local = 0.0;
            for(int i = 0; i < kCells; ++i)
                local += h_pinned[i];
            double total = 0.0;
            MPI_Allreduce(&local, &total, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            h_pinned[0] = static_cast<float>(total);
            HIP_CHECK(
                hipMemcpyAsync(d_scalar, h_pinned, sizeof(float), hipMemcpyHostToDevice, stream));
            add_scalar<<<(kCells + 255) / 256, 256, 0, stream>>>(d_c, d_scalar, kCells);
            HIP_CHECK(hipGetLastError());
        }
        else if(which == "rccl")
        {
            row_sums<<<1, kN, 0, stream>>>(d_c, d_rows, kN);
            HIP_CHECK(hipGetLastError());
            NCCL_CHECK(ncclAllReduce(d_rows, d_rows_sum, kN, ncclFloat, ncclSum, comm, stream));
        }
        HIP_CHECK(hipStreamSynchronize(stream));
    };
    auto step = [&]() {
        body();
        if(which == "rccl-after") allreduce_rows();
    };

    // Warm-up: load every code object (including RCCL's) before the range.
    reset();
    step();
    reset();
    MPI_Barrier(MPI_COMM_WORLD);

    const double t0 = MPI_Wtime();
    volatile auto begin_status = ROCPROFILER_STATUS_SUCCESS;
    if(host_mode && !declines)
    {
        if(sigsetjmp(*static_cast<sigjmp_buf*>(jmp_fn()), 1) == 0) begin_status = begin_fn(1);
    }
    else
    {
        begin_status = begin_fn(1);
    }
    if(begin_status != ROCPROFILER_STATUS_SUCCESS)
    {
        fprintf(stderr, "[rank %d] FAIL: begin returned %d\n", g_rank, (int) begin_status);
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

    body();

    if(const auto status = end_fn(); status != ROCPROFILER_STATUS_SUCCESS)
    {
        fprintf(stderr, "[rank %d] FAIL: end returned %d\n", g_rank, (int) status);
        MPI_Abort(MPI_COMM_WORLD, 2);
    }
    const double range_s = MPI_Wtime() - t0;
    if(which == "rccl-after") allreduce_rows();

    std::vector<float> got(kCells);
    HIP_CHECK(hipMemcpy(got.data(), d_c, kCells * sizeof(float), hipMemcpyDeviceToHost));
    if(uses_rccl)
    {
        got.resize(kCells + kN);
        HIP_CHECK(
            hipMemcpy(got.data() + kCells, d_rows_sum, kN * sizeof(float), hipMemcpyDeviceToHost));
    }
    const auto expect = reference(which == "rccl-after" ? "rccl" : which, g_rank, g_size);
    float      diff   = 0.f;
    for(size_t i = 0; i < expect.size(); ++i)
        diff = std::max(diff, std::fabs(got[i] - expect[i]) / std::max(1.f, std::fabs(expect[i])));

    // The communicator must still work after the range: run the body again, outside any range,
    // RR_MPI_POST_ITERS times and check each result. Replayed passes that advanced or desynchronized
    // RCCL's connection state show up here as a hang or a wrong sum.
    const int post_iters = env_int("RR_MPI_POST_ITERS", 0);
    float     post_diff  = 0.f;
    for(int iter = 0; iter < post_iters; ++iter)
    {
        reset();
        step();
        std::vector<float> again(kCells);
        HIP_CHECK(hipMemcpy(again.data(), d_c, kCells * sizeof(float), hipMemcpyDeviceToHost));
        if(uses_rccl)
        {
            again.resize(kCells + kN);
            HIP_CHECK(hipMemcpy(
                again.data() + kCells, d_rows_sum, kN * sizeof(float), hipMemcpyDeviceToHost));
        }
        for(size_t i = 0; i < expect.size(); ++i)
            post_diff = std::max(post_diff,
                                 std::fabs(again[i] - expect[i]) / std::max(1.f, std::fabs(expect[i])));
    }
    if(post_iters > 0)
        printf("[rank %d] post_iters=%d post_rel_diff=%.3g\n", g_rank, post_iters, post_diff);
    diff = std::max(diff, post_diff);

    const int local_ok = diff < 1e-4f ? 1 : 0;
    int       all_ok   = 0;
    MPI_Allreduce(&local_ok, &all_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    printf("[rank %d] case=%s mode=%s decline_rank=%d device=%d rel_diff=%.3g range_s=%.3f %s\n",
           g_rank,
           which.c_str(),
           host_mode ? "host" : "packet",
           decline_rank,
           device,
           diff,
           range_s,
           local_ok ? "ok" : "MISMATCH");
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);
    if(g_rank == 0) printf("[rr-mpi] %s\n", all_ok ? "PASS" : "FAIL");

    if(comm != nullptr) ncclCommDestroy(comm);
    MPI_Finalize();
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
