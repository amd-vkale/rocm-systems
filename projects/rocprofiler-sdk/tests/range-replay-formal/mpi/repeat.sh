#!/usr/bin/env bash
# Repeat one MPI range replay configuration and count outcomes.
#   repeat.sh <trials> <case> <mode> <decline_rank> <passes>
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
TRIALS="$1" CASE="$2" MODE="$3" DECLINE="$4" PASSES="$5"
NP="${NP:-4}"
TIMEOUT="${TIMEOUT:-45}"
OUT="${OUT:-/tmp/rr-mpi-repeat}/$CASE-$MODE-d$DECLINE-p$PASSES"
mkdir -p "$OUT"
cat >"$OUT/rank.sh" <<EOF
#!/usr/bin/env bash
export LD_LIBRARY_PATH=${SDK_LIB:-/home/vkale/work/rr-v3-build/lib}:/opt/rocm-7.14.0/lib:\${LD_LIBRARY_PATH:-}
export LD_PRELOAD=${TOOL:-/tmp/librr_tool_mpi.so}
exec $HERE/rr_mpi
EOF
chmod +x "$OUT/rank.sh"
pass=0 hang=0 fail=0
for t in $(seq 1 "$TRIALS"); do
    log="$OUT/trial-$t.log"
    timeout --signal=KILL "$TIMEOUT" mpirun --oversubscribe -np "$NP" --bind-to none \
        -x RR_MPI_CASE="$CASE" -x RR_APP_MODE="$MODE" -x RR_MPI_DECLINE_RANK="$DECLINE" \
        -x RR_HOST_REPLAY="$([ "$MODE" = host ] && echo 1 || echo 0)" -x RR_PASSES="$PASSES" \
        -x RR_COUNTERS=SQ_WAVES -x ROCPROF_RANGE_REPLAY_VERIFY=1 -x HSA_XNACK=0 -x NCCL_DEBUG=WARN \
        -x RR_MPI_AGREE="${RR_MPI_AGREE:-0}" -x RR_MPI_POST_ITERS="${RR_MPI_POST_ITERS:-5}" \
        -x RR_MPI_DEVICE_BASE="${RR_MPI_DEVICE_BASE:-4}" \
        "$OUT/rank.sh" >"$log" 2>&1
    rc=$?
    if [ $rc -eq 137 ]; then
        hang=$((hang + 1))
        pkill -KILL -f "$HERE/rr_mpi" 2>/dev/null
        sleep 1
    elif grep -q '\[rr-mpi\] PASS' "$log"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done
statuses=$(cat "$OUT"/trial-*.log | grep -oE '"status":"[A-Z_]+"' | cut -d'"' -f4 | sort | uniq -c | tr -s ' ' | tr '\n' ',')
printf '%-8s %-7s decline=%-2s passes=%s agree=%s : pass=%d hang=%d fail=%d  close=[%s]\n' \
    "$CASE" "$MODE" "$DECLINE" "$PASSES" "${RR_MPI_AGREE:-0}" "$pass" "$hang" "$fail" "$statuses"
