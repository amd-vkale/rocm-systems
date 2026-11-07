#!/bin/bash
# SQ_WAVES control: the same instrumented binary and the same counting tool, without range replay.
#   noreplay : RR_APP_MODE=none, so no range is opened; every dispatch is counted outside a range.
#   optout   : a range is opened but the tool asks for 1 pass (NO_PASS_COUNT).
# Expected SQ_WAVES for the simulation kernel: 4864 (1216 workgroups x 4 waves).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-$HERE/work}
: "${SDK_BUILD:?set SDK_BUILD to the rocprofiler-sdk build tree}"
OUT=$WORK/results/sqwaves-control
mkdir -p "$OUT"
: > "$OUT/summary.txt"
export ROCR_VISIBLE_DEVICES=${GPU:-0} HSA_XNACK=0
export LD_LIBRARY_PATH=$SDK_BUILD/lib:${LD_LIBRARY_PATH:-}
export ROCP_TOOL_LIBRARIES=$WORK/tool/librr_tool.so RR_COUNTERS=SQ_WAVES RR_COUNT_ALWAYS=1
for app in rsbench xsbench; do
    for s in small large; do
        for mode in noreplay optout; do
            for r in 0 1 2 3 4 5; do
                tag=$app-$s-$mode-r$r
                rm -f "$OUT/$tag.jsonl"
                if [ $mode = noreplay ]; then
                    RR_APP_MODE=none RR_OUT=$OUT/$tag.jsonl \
                        "$WORK/bin/$app-omp-rr" -m event -s $s > "$OUT/$tag.log" 2>&1
                else
                    RR_APP_MODE=packet RR_RANGE=kernel RR_PASSES=1 RR_OUT=$OUT/$tag.jsonl \
                        "$WORK/bin/$app-omp-rr" -m event -s $s > "$OUT/$tag.log" 2>&1
                fi
                echo "$tag $(grep -o 'checksum: [0-9]* ([A-Za-z]*' "$OUT/$tag.log") $(tr '\n' ' ' < "$OUT/$tag.jsonl")" >> "$OUT/summary.txt"
            done
        done
    done
done
