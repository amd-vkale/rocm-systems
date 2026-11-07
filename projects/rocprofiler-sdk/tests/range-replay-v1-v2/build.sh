#!/bin/bash
# Build everything the range replay v1-vs-v2 comparison runs: the SDK library pair, the validation
# tool against each SDK's own headers, the irregular-access benchmarks (C and Fortran), BabelStream
# Fortran (OpenMP target) and the XSBench / RSBench OpenMP offload ports.
#
#   SDK_V1_BUILD  build tree of range replay v1 (733091ace5c): lib/librocprofiler-sdk.so, include/
#   SDK_V1_SRC    source tree of v1 (projects/rocprofiler-sdk at 733091ace5c)
#   SDK_V2_BUILD  build tree of this branch
#   ROCM_PATH     ROCm install providing amdclang / amdflang (default /opt/rocm)
#   GPU_ARCH      offload arch (default gfx942)
#   WORK          output directory (default ./work): libs/{v1,v2}, libs/share, bin/
#
# Then: RR_LIBS=$WORK/libs RR_BIN=$WORK/bin python3 compare_v1v2.py
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
APPS=$(cd "$HERE/../range-replay-openmp-apps" && pwd)
SDK_V2_SRC=$(cd "$HERE/../.." && pwd)
: "${SDK_V1_BUILD:?set SDK_V1_BUILD to the v1 build tree}"
: "${SDK_V1_SRC:?set SDK_V1_SRC to the v1 projects/rocprofiler-sdk source tree}"
: "${SDK_V2_BUILD:?set SDK_V2_BUILD to the build tree of this branch}"
ROCM_PATH=${ROCM_PATH:-/opt/rocm}
GPU_ARCH=${GPU_ARCH:-gfx942}
WORK=${WORK:-$HERE/work}
CC=$ROCM_PATH/llvm/bin/amdclang
CXX=$ROCM_PATH/llvm/bin/amdclang++
FC=$ROCM_PATH/llvm/bin/amdflang
mkdir -p "$WORK/libs/v1" "$WORK/libs/v2" "$WORK/libs/share" "$WORK/bin" "$WORK/src"

# The SDK finds its metrics relative to the library (<libdir>/../share); v1 and v2 ship the same files.
cp -a "$SDK_V1_BUILD"/lib/librocprofiler-sdk.so* "$WORK/libs/v1/"
cp -a "$SDK_V2_BUILD"/lib/librocprofiler-sdk.so* "$WORK/libs/v2/"
cp -a "$SDK_V2_BUILD/share/rocprofiler-sdk" "$WORK/libs/share/"

tool() {
    local ver=$1 build=$2 src=$3 extra=$4
    $CXX -D__HIP_PLATFORM_AMD__ $extra -std=c++17 -O2 -fPIC -shared -I"$build/include" \
        -I"$src/source/include" -I"$ROCM_PATH/include" "$APPS/rr_tool.cpp" -L"$WORK/libs/$ver" \
        -lrocprofiler-sdk -o "$WORK/libs/$ver/librr_tool.so"
}
tool v1 "$SDK_V1_BUILD" "$SDK_V1_SRC" -DRR_TOOL_V1
tool v2 "$SDK_V2_BUILD" "$SDK_V2_SRC" ""

OMPF="-fopenmp --offload-arch=$GPU_ARCH"
$CC -O3 $OMPF -std=gnu11 -I"$APPS" "$HERE/irregular/irregular.c" -o "$WORK/bin/irregular-c" -ldl -lm
$CC -O2 -fPIC -I"$APPS" -c "$HERE/irregular/rr_f.c" -o "$WORK/src/rr_f.o"
(cd "$WORK/src" && $FC -O3 $OMPF "$HERE/irregular/irregular.f90" rr_f.o -o "$WORK/bin/irregular-f" -ldl)

clone() {
    local url=$1 dir=$2 sha=$3
    [ -d "$dir" ] || git clone -q "$url" "$dir"
    git -C "$dir" checkout -q "$sha"
}
clone https://github.com/UoB-HPC/BabelStream.git "$WORK/src/BabelStream" 5cb9445
git -C "$WORK/src/BabelStream" checkout -q -- src/fortran/main.F90
git -C "$WORK/src/BabelStream" apply "$HERE/babelstream/babelstream-fortran-rr.patch"
(
    cd "$WORK/src/BabelStream/src/fortran"
    BF="-cpp -O3 $OMPF -DUSE_OPENMPTARGET -DVERSION_STRING=4.0"
    $FC $BF -c BabelStreamTypes.F90
    $FC $BF -c OpenMPTargetStream.F90
    $FC $BF -c main.F90
    $FC $BF BabelStreamTypes.o OpenMPTargetStream.o main.o "$WORK/src/rr_f.o" \
        -o "$WORK/bin/babelstream-f" -ldl
)

clone https://github.com/ANL-CESAR/XSBench.git "$WORK/src/XSBench" ba08e52
clone https://github.com/ANL-CESAR/RSBench.git "$WORK/src/RSBench" 34b6447
rm -rf "$WORK/src/apps" && mkdir -p "$WORK/src/apps"
cp -r "$WORK/src/XSBench/openmp-offload" "$WORK/src/apps/xsbench-omp"
cp -r "$WORK/src/RSBench/openmp-offload" "$WORK/src/apps/rsbench-omp"
cp "$APPS/rr_app.h" "$APPS/rr_rt.h" "$WORK/src/apps/"
(cd "$WORK/src/apps/xsbench-omp" && patch -s -p2 < "$APPS/patches/xsbench-openmp-offload.patch")
(cd "$WORK/src/apps/rsbench-omp" && patch -s -p2 < "$APPS/patches/rsbench-openmp-offload.patch")
(cd "$WORK/src/apps/xsbench-omp" && $CC $OMPF -O3 -std=gnu99 ./*.c -lm -ldl -o "$WORK/bin/xsbench-omp-rr")
(cd "$WORK/src/apps/rsbench-omp" && $CC $OMPF -O3 -std=gnu99 ./*.c -lm -ldl -o "$WORK/bin/rsbench-omp-rr")
echo "built into $WORK"
