#!/usr/bin/env python3
"""Before/after comparison of range replay v1 (packet only) and v2 (packet + host replay).

Every benchmark binary resolves the range replay API at run time, so the same binary runs
natively, against the v1 SDK, and against the v2 SDK. The SDK under test is pinned with
LD_PRELOAD + LD_LIBRARY_PATH, and the tool is built against that SDK's own headers.

Row per run: benchmark, config, range kind, exit code, app validity and result hash, the app's
range wall time, and the tool's CLOSE record (status, dispatches, per-pass ms, per-pass counters).
"""

import argparse
import concurrent.futures as cf
import json
import os
import queue
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
LIBS = os.environ.get("RR_LIBS", "/tmp/rr-libs")
# RR_BIN holds every benchmark binary (build.sh's layout); unset, the development tree is used.
if os.environ.get("RR_BIN"):
    IRR = BIN = BABEL = os.environ["RR_BIN"]
else:
    IRR = os.path.join(ROOT, "irregular")
    BIN = os.path.join(ROOT, "bin")
    BABEL = os.path.join(ROOT, "babelstream")

# name -> (argv, ranges it supports)
BENCHES = {}
for b in ("spmv", "gups", "hist", "gather", "chase"):
    BENCHES[f"c-{b}"] = ([os.path.join(IRR, "irregular-c"), b], ("kernel", "full"))
for b in ("spmv", "gups", "hist", "gather", "chase", "jacobi"):
    BENCHES[f"f-{b}"] = ([os.path.join(IRR, "irregular-f"), b], ("kernel", "full"))
BENCHES["f-babelstream"] = ([os.path.join(BABEL, "babelstream-f"), "-n", "20"], ("kernel",))
BENCHES["f-babel-triad"] = ([os.path.join(BABEL, "babelstream-f"), "-n", "20", "--triad-only"],
                            ("kernel",))
for b in ("xsbench", "rsbench"):
    BENCHES[f"c-{b}"] = ([os.path.join(BIN, f"{b}-omp-rr"), "-m", "event", "-s", "small"],
                         ("kernel", "full"))

# name -> (sdk version or None, app mode, host replay, extra env)
CONFIGS = {
    "native": (None, "none", False, {}),
    "v1-packet": ("v1", "packet", False, {}),
    "v2-packet": ("v2", "packet", False, {}),
    "v2-host": ("v2", "host", True, {}),
    "v1-host": ("v1", "host", True, {}),
    # Negative control: host replay without registering the host arrays the range writes.
    "v2-host-notrack": ("v2", "host", True, {"RR_NO_TRACK": "1"}),
    # libomptarget's memory pool keeps freed device blocks, so an allocation made inside the range
    # outlives it; threshold 0 returns every block to HSA at free.
    "native-nomm": (None, "none", False, {"LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0"}),
    "v1-packet-nomm": ("v1", "packet", False, {"LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0"}),
    "v2-packet-nomm": ("v2", "packet", False, {"LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0"}),
    "v2-host-nomm": ("v2", "host", True, {"LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0"}),
}


def parse_app(out):
    r = {"valid": None, "hash": None, "app_range_ms": None, "err": None}
    m = re.search(r"\[bench\][^\n]*?range_ms=\s*([0-9.]+)", out)
    if m:
        r["app_range_ms"] = float(m.group(1))
    m = re.search(r"\[bench\][^\n]*?hash=(\w+)", out)
    if m:
        r["hash"] = m.group(1)
    m = re.search(r"\[bench\][^\n]*?err=\s*(\S+)", out)
    if m:
        r["err"] = m.group(1)
    m = re.search(r"\[bench\][^\n]*?valid=(\d)", out)
    if m:
        r["valid"] = m.group(1) == "1"
    ck = re.search(r"Verification checksum: (\d+) \((\w+)\)", out)
    if ck:
        r["hash"] = ck.group(1)
        r["valid"] = ck.group(2) == "Valid"
    rt = re.search(r"Runtime:\s+([0-9.]+) seconds", out)
    if rt:
        r["app_runtime_s"] = float(rt.group(1))
    if "BabelStream" in out:
        # check_solution does `error stop 1` on a wrong answer; a clean exit means it passed.
        r["valid"] = "Function" in out and "Validation failed" not in out
        bw = re.findall(r"^(Copy|Mul|Add|Triad|Dot)\s+([0-9.]+)", out, re.M)
        r["babel_mbps"] = {k: float(v) for k, v in bw}
    return r


def run_one(bench, cfg, rng, rep, passes, counters, gpu, outdir, xnack):
    argv, _ = BENCHES[bench]
    sdk, mode, host, extra = CONFIGS[cfg]
    env = dict(os.environ)
    for k in ("LD_PRELOAD", "ROCP_TOOL_LIBRARIES", "RR_OUT"):
        env.pop(k, None)
    env["ROCR_VISIBLE_DEVICES"] = str(gpu)
    env["HSA_XNACK"] = "1" if xnack else "0"
    env["RR_APP_MODE"] = mode
    env["RR_RANGE"] = rng
    env.update(extra)
    tag = f"{bench}-{cfg}-{rng}-x{int(xnack)}-r{rep}"
    rr_out = os.path.join(outdir, tag + ".close.jsonl")
    if os.path.exists(rr_out):
        os.remove(rr_out)
    if sdk:
        lib = os.path.join(LIBS, sdk)
        env["LD_PRELOAD"] = os.path.join(lib, "librocprofiler-sdk.so.1")
        env["LD_LIBRARY_PATH"] = lib + ":" + env.get("LD_LIBRARY_PATH", "")
        env["ROCP_TOOL_LIBRARIES"] = os.path.join(lib, "librr_tool.so")
        env["RR_PASSES"] = str(passes)
        env["RR_HOST_REPLAY"] = "1" if host else "0"
        env["RR_OUT"] = rr_out
        if counters:
            env["RR_COUNTERS"] = counters
    t0 = time.time()
    try:
        p = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=900,
                           cwd=os.path.dirname(argv[0]))
        rc, out = p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired as e:
        rc, out = "timeout", (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    wall = time.time() - t0
    with open(os.path.join(outdir, tag + ".log"), "w") as f:
        f.write(out)
    row = {"tag": tag, "bench": bench, "config": cfg, "range": rng, "rep": rep, "xnack": xnack,
           "passes": passes if sdk else 1, "exit": rc, "wall_s": round(wall, 3)}
    row.update(parse_app(out))
    closes = []
    if os.path.exists(rr_out):
        with open(rr_out) as f:
            closes = [json.loads(l) for l in f if l.strip()]
    row["close"] = closes
    row["notes"] = sorted(set(
        l.strip()[:240] for l in out.splitlines()
        if re.search(r"rr-app\]|declin|unsupported|not in this", l, re.I)))[:6]
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--benches", default=",".join(BENCHES))
    ap.add_argument("--configs", default="native,v1-packet,v2-packet,v2-host")
    ap.add_argument("--ranges", default="kernel,full")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--passes", type=int, default=4)
    ap.add_argument("--counters", default="SQ_WAVES")
    ap.add_argument("--gpus", default="5,6,7")
    ap.add_argument("--xnack", type=int, default=0)
    ap.add_argument("--out", default=os.path.join(ROOT, "results-v1v2"))
    a = ap.parse_args()
    # Benchmarks run from their own directory, so RR_OUT must not be relative.
    a.out = os.path.abspath(a.out)
    os.makedirs(a.out, exist_ok=True)
    gpus = [int(g) for g in a.gpus.split(",")]
    benches = a.benches.split(",")
    free = queue.Queue()
    for g in gpus:
        free.put(g)

    def shard(bench):
        gpu = free.get()
        try:
            return shard_on(bench, gpu)
        finally:
            free.put(gpu)

    def shard_on(bench, gpu):
        rows = []
        for rng in a.ranges.split(","):
            if rng not in BENCHES[bench][1]:
                continue
            for rep in range(a.reps):
                for cfg in a.configs.split(","):
                    row = run_one(bench, cfg, rng, rep, a.passes, a.counters, gpu, a.out,
                                  bool(a.xnack))
                    c = row["close"][0] if row["close"] else {}
                    print(f"{row['tag']:44s} gpu={gpu} exit={row['exit']} valid={row['valid']} "
                          f"hash={row['hash']} app_ms={row['app_range_ms']} "
                          f"status={c.get('status')} disp={c.get('dispatch_count')} "
                          f"pass_ms={[round(v, 1) for v in c.get('pass_ms', [])]}", flush=True)
                    rows.append(row)
        return rows

    res_file = os.path.join(a.out, "results.jsonl")
    with cf.ThreadPoolExecutor(max_workers=len(gpus)) as ex:
        futs = [ex.submit(shard, b) for b in benches]
        for f in cf.as_completed(futs):
            with open(res_file, "a") as fh:
                for row in f.result():
                    fh.write(json.dumps(row) + "\n")


if __name__ == "__main__":
    sys.exit(main())
