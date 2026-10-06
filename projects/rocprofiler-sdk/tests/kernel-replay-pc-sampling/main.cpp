// MIT License
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The same busy kernel runs twice with the same number of waves: once with the block size the
// tool replays, once with a block size it leaves alone. The second launch is the tool's reference
// for how many PC samples one execution of the kernel produces.

#include <hip/hip_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define HIP_CHECK(call)                                                                            \
    do                                                                                             \
    {                                                                                              \
        hipError_t _err = (call);                                                                  \
        if(_err != hipSuccess)                                                                     \
        {                                                                                          \
            fprintf(                                                                               \
                stderr, "HIP error '%s' at %s:%d\n", hipGetErrorString(_err), __FILE__, __LINE__); \
            return EXIT_FAILURE;                                                                   \
        }                                                                                          \
    } while(0)

constexpr int kReplayBlock      = 192;
constexpr int kReplayGrid       = 2048;
constexpr int kReferenceBlock   = 256;
constexpr int kReferenceGrid    = 1536;
constexpr int kThreadsPerLaunch = kReplayBlock * kReplayGrid;
static_assert(kThreadsPerLaunch == kReferenceBlock * kReferenceGrid);

__global__ void
spin(float* out, int iterations)
{
    float acc = (threadIdx.x % 64) * 0.5f;
    for(int i = 0; i < iterations; ++i)
        acc = acc * 1.000001f + 0.5f;
    out[blockIdx.x * blockDim.x + threadIdx.x] += acc;
}

int
main()
{
    const int iterations = 200000;
    float*    out        = nullptr;
    HIP_CHECK(hipMalloc(&out, 2 * kThreadsPerLaunch * sizeof(float)));
    HIP_CHECK(hipMemset(out, 0, 2 * kThreadsPerLaunch * sizeof(float)));
    HIP_CHECK(hipDeviceSynchronize());

    spin<<<kReplayGrid, kReplayBlock>>>(out, iterations);
    HIP_CHECK(hipGetLastError());
    HIP_CHECK(hipDeviceSynchronize());

    spin<<<kReferenceGrid, kReferenceBlock>>>(out + kThreadsPerLaunch, iterations);
    HIP_CHECK(hipGetLastError());
    HIP_CHECK(hipDeviceSynchronize());

    // Both launches add the same value once; a replay that did not restore between passes would
    // have added it once per pass.
    float replayed = 0, reference = 0;
    HIP_CHECK(hipMemcpy(&replayed, out, sizeof(float), hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(&reference, out + kThreadsPerLaunch, sizeof(float), hipMemcpyDeviceToHost));
    HIP_CHECK(hipFree(out));

    printf("[app] replayed=%g reference=%g\n", replayed, reference);
    if(!(reference > 0) || std::fabs(replayed - reference) > 1e-3f * reference)
    {
        fprintf(stderr, "[app] FAIL: the replayed launch was not applied exactly once\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
