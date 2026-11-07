#!/bin/bash
# Fetch XSBench / RSBench at the validated commits, apply the range replay instrumentation, and build
# the OpenMP offload ports (pristine and instrumented) plus the validation tool.
#
#   SDK_BUILD   rocprofiler-sdk build tree (contains lib/librocprofiler-sdk.so and include/)
#   ROCM_PATH   ROCm install providing amdclang (default /opt/rocm)
#   GPU_ARCH    offload arch (default gfx942)
#   WORK        scratch directory for sources and binaries (default ./work)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SDK_SRC_INC=$(cd "$HERE/../../source/include" && pwd)
: "${SDK_BUILD:?set SDK_BUILD to the rocprofiler-sdk build tree}"
ROCM_PATH=${ROCM_PATH:-/opt/rocm}
GPU_ARCH=${GPU_ARCH:-gfx942}
WORK=${WORK:-$HERE/work}
mkdir -p "$WORK/bin" "$WORK/tool" "$WORK/apps"

clone() {
    local url=$1 dir=$2 sha=$3
    [ -d "$dir" ] || git clone -q "$url" "$dir"
    git -C "$dir" checkout -q "$sha"
}
clone https://github.com/ANL-CESAR/XSBench.git "$WORK/XSBench" ba08e52
clone https://github.com/ANL-CESAR/RSBench.git "$WORK/RSBench" 34b6447

rm -rf "$WORK/apps/xsbench-omp" "$WORK/apps/rsbench-omp"
cp -r "$WORK/XSBench/openmp-offload" "$WORK/apps/xsbench-omp"
cp -r "$WORK/RSBench/openmp-offload" "$WORK/apps/rsbench-omp"
cp "$HERE/rr_app.h" "$HERE/rr_rt.h" "$WORK/apps/"
(cd "$WORK/apps/xsbench-omp" && patch -s -p2 < "$HERE/patches/xsbench-openmp-offload.patch")
(cd "$WORK/apps/rsbench-omp" && patch -s -p2 < "$HERE/patches/rsbench-openmp-offload.patch")

CC=$ROCM_PATH/llvm/bin/amdclang
CXX=$ROCM_PATH/llvm/bin/amdclang++
INC="-I$SDK_BUILD/include -I$SDK_SRC_INC -I$ROCM_PATH/include"
LIBS="-L$SDK_BUILD/lib -Wl,-rpath,$SDK_BUILD/lib -lrocprofiler-sdk"
OMPF="-fopenmp --offload-arch=$GPU_ARCH -O3 -std=gnu99"

$CXX -D__HIP_PLATFORM_AMD__ -std=c++17 -O2 -fPIC -shared $INC "$HERE/rr_tool.cpp" $LIBS \
    -o "$WORK/tool/librr_tool.so"
# The apps resolve the range replay API at run time (rr_rt.h) and do not link the SDK.
(cd "$WORK/apps/xsbench-omp" && $CC $OMPF ./*.c -lm -ldl -o "$WORK/bin/xsbench-omp-rr")
(cd "$WORK/apps/rsbench-omp" && $CC $OMPF ./*.c -lm -ldl -o "$WORK/bin/rsbench-omp-rr")
(cd "$WORK/XSBench/openmp-offload" && $CC $OMPF ./*.c -lm -o "$WORK/bin/xsbench-omp-pristine")
(cd "$WORK/RSBench/openmp-offload" && $CC $OMPF ./*.c -lm -o "$WORK/bin/rsbench-omp-pristine")
echo "built into $WORK"
