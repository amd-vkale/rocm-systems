#!/usr/bin/env bash
export HIP_VISIBLE_DEVICES=${DEV:-5}
export LD_LIBRARY_PATH=${SDK_LIB:-/home/vkale/work/rr-v3-build/lib}:/opt/rocm-7.14.0/lib
export LD_PRELOAD=${TOOL:-/tmp/librr_tool_latest.so}
export RR_HOST_REPLAY=$([ "${RR_APP_MODE:-packet}" = host ] && echo 1 || echo 0)
export RR_PASSES=${RR_PASSES:-4} RR_COUNTERS=${RR_COUNTERS:-SQ_WAVES}
exec "$(dirname "$0")/${APP:-rr_copy_race}"
