#!/usr/bin/env bash
# Range replay across MPI ranks: case x mode x decline, each under a timeout so a deadlock is
# reported instead of hanging the matrix.
#
#   NP=4 TIMEOUT=90 OUT=/tmp/rr-mpi ./run_mpi_matrix.sh [case ...]

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
NP="${NP:-4}"
TIMEOUT="${TIMEOUT:-90}"
OUT="${OUT:-/tmp/rr-mpi}"
SDK_LIB="${SDK_LIB:-/home/vkale/work/rr-v3-build/lib}"
TOOL="${TOOL:-/tmp/librr_tool_latest.so}"
CASES=("$@")
[ ${#CASES[@]} -eq 0 ] && CASES=(compute host rccl)

mkdir -p "$OUT"

cat >"$OUT/rank.sh" <<EOF
#!/usr/bin/env bash
export LD_LIBRARY_PATH=$SDK_LIB:/opt/rocm-7.14.0/lib:\${LD_LIBRARY_PATH:-}
export LD_PRELOAD=$TOOL
exec $HERE/rr_mpi
EOF
chmod +x "$OUT/rank.sh"

run_one() {
    local name="$1" case_name="$2" mode="$3" decline="$4"
    local log="$OUT/$name.log"
    timeout --signal=KILL "$TIMEOUT" mpirun --oversubscribe -np "$NP" --bind-to none \
        -x RR_MPI_CASE="$case_name" -x RR_APP_MODE="$mode" -x RR_MPI_DECLINE_RANK="$decline" \
        -x RR_HOST_REPLAY="$([ "$mode" = host ] && echo 1 || echo 0)" \
        -x RR_PASSES=4 -x RR_COUNTERS=SQ_WAVES -x ROCPROF_RANGE_REPLAY_VERIFY=1 -x HSA_XNACK=0 \
        -x NCCL_DEBUG=WARN -x RR_MPI_AGREE="${RR_MPI_AGREE:-0}" \
        -x RR_MPI_DEVICE_BASE="${RR_MPI_DEVICE_BASE:-4}" -x RR_MPI_POST_ITERS="${RR_MPI_POST_ITERS:-0}" \
        "$OUT/rank.sh" >"$log" 2>&1
    local rc=$?
    local verdict
    if [ $rc -eq 137 ]; then
        verdict="HANG (killed after ${TIMEOUT}s)"
        pkill -KILL -f "$HERE/rr_mpi" 2>/dev/null
    elif grep -q '\[rr-mpi\] PASS' "$log"; then
        verdict="PASS"
    else
        verdict="FAIL rc=$rc"
    fi
    local statuses
    statuses=$(grep -oE '"mode":"[a-z]+","passes_requested":[0-9]+,"status":"[A-Z_]+"' "$log" |
        grep -oE '"status":"[A-Z_]+"' | cut -d'"' -f4 | sort | uniq -c | tr -s ' ' | tr '\n' ',')
    local late
    late=$(grep -c '"late_records":\[\]' "$log")
    printf '%-28s %-30s close_status=[%s] clean_fini=%s/%s\n' "$name" "$verdict" "$statuses" "$late" "$NP"
}

for c in "${CASES[@]}"; do
    for m in packet host; do
        run_one "$c-$m-sym" "$c" "$m" -1
        run_one "$c-$m-decline1" "$c" "$m" 1
    done
done
