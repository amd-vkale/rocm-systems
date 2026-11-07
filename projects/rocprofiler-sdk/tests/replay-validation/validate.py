#!/usr/bin/env python3
# MIT License
#
# Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
#
# Correctness check for the replay integration preview. It runs the kernel replay and range replay
# perf workloads once each, under their own preloaded client, and checks the result the application
# reads back after replay. Both workloads bump a device counter once per dispatch they issue
# themselves and rely on the replay restoring that state, so the counter is a direct statement
# about whether the replay handed the application its own result back:
#
#   kernel replay   counter == launches          (one increment per application launch)
#   range replay    counter == dispatches*ranges (one increment per dispatch the application issued)
#
# A replay that leaked a pass's writes past its restore window fails here. The apps already exit
# non-zero on that condition; this script checks it again from their summary line so the expected
# value is visible in the output, and prints a single [replay-validate] line for ctest.
#
# The optional --openmp cases point at the XSBench and RSBench OpenMP-offload binaries range
# replay was validated against. Those are not built by this tree. Each case is
# LABEL:BIN:TOOL:HASH:STATUS -- the instrumented binary, the range replay tool to preload, the
# verification checksum the app prints and the range status the tool reports. The
# hash is the app's own checksum, so it passing says the replay reproduced the run.

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


def preload_env(client: Path) -> dict:
    env = os.environ.copy()
    preload = str(client.resolve())
    env["LD_PRELOAD"] = (
        f"{preload}:{env['LD_PRELOAD']}" if env.get("LD_PRELOAD") else preload
    )
    return env


def run(cmd: list, env: dict) -> tuple:
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True, check=False)
    return proc.returncode, proc.stdout + proc.stderr


def field(out: str, marker: str, name: str) -> int:
    """The integer value of `name` on the workload's summary line."""
    match = re.search(rf"^\[{marker}\] .*\b{name}=(\d+)", out, re.MULTILINE)
    if not match:
        raise RuntimeError(f"no {name} on the [{marker}] summary line")
    return int(match.group(1))


def check_kernel_replay(testapp: Path, client: Path, passes: int) -> None:
    ballast, launches, warmup = 32, 4, 1
    env = preload_env(client)
    env["KR_PERF_PASSES"] = str(passes)
    rc, out = run(
        [str(testapp.resolve()), str(ballast), str(launches), str(warmup)], env
    )
    if rc != 0 or "[kr-perf] PASS" not in out:
        raise RuntimeError(f"kernel replay rc={rc}\n{out}")
    counter = field(out, "kr-perf", "counter")
    expected = field(out, "kr-perf", "launches")
    if counter != expected:
        raise RuntimeError(
            f"kernel replay counter={counter} expected {expected} (launches)\n{out}"
        )
    print(
        f"[replay-validate] kernel-replay PASS counter={counter} "
        f"launches={expected} passes={passes}",
        flush=True,
    )


def check_range_replay(testapp: Path, client: Path, passes: int) -> None:
    ballast, dispatches, ranges, warmup = 32, 4, 2, 1
    env = preload_env(client)
    env["RR_PERF_PASSES"] = str(passes)
    rc, out = run(
        [
            str(testapp.resolve()),
            str(ballast),
            str(dispatches),
            str(ranges),
            str(warmup),
        ],
        env,
    )
    if rc != 0 or "[rr-perf] PASS" not in out:
        raise RuntimeError(f"range replay rc={rc}\n{out}")
    counter = field(out, "rr-perf", "counter")
    expected = field(out, "rr-perf", "dispatches") * field(out, "rr-perf", "ranges")
    if counter != expected:
        raise RuntimeError(
            f"range replay counter={counter} expected {expected} "
            f"(dispatches*ranges)\n{out}"
        )
    print(
        f"[replay-validate] range-replay PASS counter={counter} "
        f"expected={expected} passes={passes}",
        flush=True,
    )


def check_openmp(case: str) -> None:
    """LABEL:BIN:TOOL:HASH:STATUS. HASH is the app's `hash=` value, STATUS the range status."""
    try:
        label, bin_path, tool, expect_hash, expect_status = case.split(":")
    except ValueError as exc:
        raise RuntimeError(
            f"--openmp case {case!r} is not LABEL:BIN:TOOL:HASH:STATUS"
        ) from exc

    env = preload_env(Path(tool))
    rc, out = run([bin_path], env)
    if rc != 0:
        raise RuntimeError(f"{label} rc={rc}\n{out}")
    # The app prints its checksum as `hash=<n>` or `Verification checksum: <n>`; either counts.
    if not re.search(rf"(?<!\d){re.escape(expect_hash)}(?!\d)", out):
        raise RuntimeError(f"{label} checksum {expect_hash} not in output\n{out}")
    if expect_status not in out:
        raise RuntimeError(f"{label} status {expect_status} not in output\n{out}")
    print(
        f"[replay-validate] {label} PASS hash={expect_hash} status={expect_status}",
        flush=True,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kr-testapp", type=Path, required=True)
    ap.add_argument("--kr-client", type=Path, required=True)
    ap.add_argument("--rr-testapp", type=Path, required=True)
    ap.add_argument("--rr-client", type=Path, required=True)
    ap.add_argument("--passes", type=int, default=4)
    ap.add_argument(
        "--openmp",
        action="append",
        default=[],
        metavar="LABEL:BIN:TOOL:HASH:STATUS",
        help="an XSBench or RSBench case to check as well; repeatable",
    )
    args = ap.parse_args()

    if args.passes < 2:
        print("[replay-validate] FAIL passes must be >= 2", flush=True)
        return 1

    try:
        check_kernel_replay(args.kr_testapp, args.kr_client, args.passes)
        check_range_replay(args.rr_testapp, args.rr_client, args.passes)
        for case in args.openmp:
            check_openmp(case)
    except RuntimeError as exc:
        print(f"[replay-validate] FAIL {exc}", flush=True)
        return 1

    print("[replay-validate] PASS", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
