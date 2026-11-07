#!/usr/bin/env python3
"""Run XSBench / RSBench (OpenMP offload) under range replay and record hard results.

Each row: app, size, config, runtime, checksum, app validity, and the CLOSE record(s) the
validation tool wrote (status, dispatches, divergence, per-pass ms, per-pass counters).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get("WORK", os.path.join(ROOT, "work"))
SDK_LIB = os.path.join(os.environ["SDK_BUILD"], "lib")
TOOL = os.path.join(WORK, "tool", "librr_tool.so")
BIN = os.path.join(WORK, "bin")

COUNTERS = os.environ.get("RR_MATRIX_COUNTERS", "SQ_WAVES,SQ_INSTS_VALU,SQ_INSTS_SALU,SQ_INSTS_VMEM_RD")

# name -> (binary suffix, env, use tool)
CONFIGS = {
    "pristine": ("pristine", {}, False),
    "instr-notool": ("rr", {"RR_APP_MODE": "none"}, False),
    "kernel-packet": ("rr", {"RR_APP_MODE": "packet", "RR_RANGE": "kernel", "RR_HOST_REPLAY": "0"}, True),
    "kernel-host": ("rr", {"RR_APP_MODE": "host", "RR_RANGE": "kernel", "RR_HOST_REPLAY": "1"}, True),
    "full-packet": ("rr", {"RR_APP_MODE": "packet", "RR_RANGE": "full", "RR_HOST_REPLAY": "0"}, True),
    "full-host": ("rr", {"RR_APP_MODE": "host", "RR_RANGE": "full", "RR_HOST_REPLAY": "1"}, True),
    "full-host-nomm": (
        "rr",
        {"RR_APP_MODE": "host", "RR_RANGE": "full", "RR_HOST_REPLAY": "1",
         "LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0"},
        True,
    ),
    "full-host-nomm-warm": (
        "rr",
        {"RR_APP_MODE": "host", "RR_RANGE": "full", "RR_HOST_REPLAY": "1", "RR_WARMUP": "1",
         "LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0"},
        True,
    ),
    "kernel-host-nomm": (
        "rr",
        {"RR_APP_MODE": "host", "RR_RANGE": "kernel", "RR_HOST_REPLAY": "1",
         "LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0"},
        True,
    ),
    "kernel-packet-ctr": (
        "rr", {"RR_APP_MODE": "packet", "RR_RANGE": "kernel", "RR_HOST_REPLAY": "0",
               "RR_COUNTERS": COUNTERS}, True),
    "kernel-host-nomm-ctr": (
        "rr", {"RR_APP_MODE": "host", "RR_RANGE": "kernel", "RR_HOST_REPLAY": "1",
               "LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0", "RR_COUNTERS": COUNTERS}, True),
    "full-host-nomm-warm-ctr": (
        "rr", {"RR_APP_MODE": "host", "RR_RANGE": "full", "RR_HOST_REPLAY": "1", "RR_WARMUP": "1",
               "LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD": "0", "RR_COUNTERS": COUNTERS}, True),
    "kernel-host-unarmed": ("rr", {"RR_APP_MODE": "packet", "RR_RANGE": "kernel", "RR_HOST_REPLAY": "1"}, True),
}

XNACK_ENV = {"HSA_XNACK": "1", "OMPX_APU_MAPS": "1"}


def parse(app, out):
    rt = re.search(r"Runtime:\s+([0-9.]+) seconds", out)
    ck = re.search(r"Verification checksum: (\d+) \((\w+)\)", out)
    passes = re.findall(r"\[rr-app\] \w+ host_pass=(\d+) hash=(\d+)", out)
    return {
        "runtime_s": float(rt.group(1)) if rt else None,
        "checksum": int(ck.group(1)) if ck else None,
        "valid": (ck.group(2) == "Valid") if ck else False,
        "host_pass_hashes": [[int(p), int(h)] for p, h in passes],
        "on_device": ("Kernel ran accelerator device" in out) if app == "rsbench" else None,
    }


def run_one(app, size, cfg, xnack, verify, passes, rep, outdir, gpu):
    suffix, env_extra, use_tool = CONFIGS[cfg]
    exe = os.path.join(BIN, f"{app}-omp-{suffix}")
    env = dict(os.environ)
    env["ROCR_VISIBLE_DEVICES"] = str(gpu)
    env["LD_LIBRARY_PATH"] = SDK_LIB + ":" + env.get("LD_LIBRARY_PATH", "")
    env.update(env_extra)
    if xnack:
        env.update(XNACK_ENV)
    else:
        env["HSA_XNACK"] = "0"
    tag = f"{app}-{size}-{cfg}-x{int(xnack)}-v{int(verify)}-p{passes}-r{rep}"
    rr_out = os.path.join(outdir, tag + ".close.jsonl")
    if os.path.exists(rr_out):
        os.remove(rr_out)
    if use_tool:
        env["ROCP_TOOL_LIBRARIES"] = TOOL
        env["RR_PASSES"] = str(passes)
        env["RR_OUT"] = rr_out
        if verify:
            env["ROCPROF_RANGE_REPLAY_VERIFY"] = "1"
    t0 = time.time()
    p = subprocess.run([exe, "-m", "event", "-s", size], env=env, capture_output=True, text=True,
                       timeout=1800)
    wall = time.time() - t0
    out = p.stdout + p.stderr
    with open(os.path.join(outdir, tag + ".log"), "w") as f:
        f.write(out)
    row = {"tag": tag, "app": app, "size": size, "config": cfg, "xnack": xnack, "verify": verify,
           "passes": passes if use_tool else 1, "rep": rep, "exit": p.returncode,
           "wall_s": round(wall, 3)}
    row.update(parse(app, out))
    closes = []
    if os.path.exists(rr_out):
        with open(rr_out) as f:
            closes = [json.loads(l) for l in f if l.strip()]
    row["close"] = closes
    row["sdk_warnings"] = sorted(set(
        l.strip()[:240] for l in out.splitlines()
        if re.search(r"range.replay|range_replay", l, re.I) and re.search(r"warn|error|declin", l, re.I)))[:8]
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apps", default="xsbench,rsbench")
    ap.add_argument("--sizes", default="small,large")
    ap.add_argument("--configs", default=",".join(CONFIGS))
    ap.add_argument("--xnack", default="0")
    ap.add_argument("--verify", default="1")
    ap.add_argument("--passes", type=int, default=4)
    ap.add_argument("--reps", type=int, default=1)
    ap.add_argument("--gpu", type=int, default=0)
    ap.add_argument("--out", default=os.path.join(WORK, "results"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    res_file = os.path.join(a.out, "results.jsonl")
    for app in a.apps.split(","):
        for size in a.sizes.split(","):
            for x in [bool(int(v)) for v in a.xnack.split(",")]:
                for cfg in a.configs.split(","):
                    for verify in [bool(int(v)) for v in a.verify.split(",")]:
                        for rep in range(a.reps):
                            row = run_one(app, size, cfg, x, verify, a.passes, rep, a.out, a.gpu)
                            with open(res_file, "a") as f:
                                f.write(json.dumps(row) + "\n")
                            c = row["close"][0] if row["close"] else {}
                            print(f"{row['tag']:60s} exit={row['exit']} rt={row['runtime_s']} "
                                  f"ck={row['checksum']} valid={row['valid']} "
                                  f"status={c.get('status')} disp={c.get('dispatch_count')} "
                                  f"div={c.get('divergence_count')} "
                                  f"pass_ms={[round(v, 1) for v in c.get('pass_ms', [])]}",
                                  flush=True)


if __name__ == "__main__":
    sys.exit(main())
