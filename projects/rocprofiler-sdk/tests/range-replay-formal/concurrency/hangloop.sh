#!/usr/bin/env bash
# Runs a command repeatedly; on the first run that outlives LIMIT seconds, dumps every thread's
# stack with gdb and stops.   ITER=20 LIMIT=60 OUT=/tmp/hang ./hangloop.sh cmd args...
ITER=${ITER:-20}
LIMIT=${LIMIT:-60}
OUT=${OUT:-/tmp/hangloop}
mkdir -p "$OUT"
for i in $(seq 1 "$ITER"); do
    "$@" > "$OUT/run-$i.log" 2>&1 &
    pid=$!
    for _ in $(seq 1 "$LIMIT"); do
        kill -0 $pid 2>/dev/null || break
        sleep 1
    done
    if kill -0 $pid 2>/dev/null; then
        # The command may be a wrapper; dump the deepest descendant that is still running.
        target=$pid
        while child=$(pgrep -P "$target" | head -1); [ -n "$child" ]; do target=$child; done
        if [ -n "${GDB_WRAPPED:-}" ]; then
            # The command runs under gdb: interrupt the inferior so gdb prints the stacks.
            kill -INT "$target" 2>/dev/null
            sleep 20
            cp "$OUT/run-$i.log" "$OUT/hang-$i.bt"
        else
            gdb -q -batch -p "$target" -ex 'thread apply all bt 40' > "$OUT/hang-$i.bt" 2>&1
        fi
        kill -9 $pid "$target" 2>/dev/null
        echo "iteration $i: HANG (stacks in $OUT/hang-$i.bt)"
        exit 1
    fi
    wait $pid
    echo "iteration $i: rc=$? $(grep -E '^\[app\] (PASS|FAIL)' "$OUT/run-$i.log" | head -1)"
done
