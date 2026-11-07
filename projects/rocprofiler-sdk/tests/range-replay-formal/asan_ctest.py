"""Runs ctest-registered tests without the sanitizer runtime in LD_PRELOAD.

The ASan build's executables carry clang's ASan runtime statically; ctest's environment preloads a
second runtime, which aborts at startup. This replays each selected test's command, environment,
working directory and pass/fail regexes from `ctest --show-only=json-v1` with that entry removed.
    python3 asan_ctest.py <build-dir> <ctest -R regex> [ctest -E regex]"""
import concurrent.futures
import json
import os
import re
import subprocess
import sys

build, include = sys.argv[1], sys.argv[2]
exclude = sys.argv[3] if len(sys.argv) > 3 else None
args = ["ctest", "--show-only=json-v1", "-R", include] + (["-E", exclude] if exclude else [])
tests = json.loads(subprocess.run(args, cwd=build, capture_output=True, text=True,
                                  check=True).stdout)["tests"]


def props(t):
    return {p["name"]: p["value"] for p in t.get("properties", [])}


def run(t):
    p = props(t)
    if "command" not in t:
        return t["name"], "NO-COMMAND", ""
    env = dict(os.environ)
    for kv in p.get("ENVIRONMENT", []):
        k, _, v = kv.partition("=")
        env[k] = v
    if "LD_PRELOAD" in env:
        env["LD_PRELOAD"] = ":".join(x for x in env["LD_PRELOAD"].split(":")
                                     if x and "asan" not in os.path.basename(x))
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=0")
    try:
        r = subprocess.run(t["command"], cwd=p.get("WORKING_DIRECTORY", build), env=env,
                           capture_output=True, text=True, timeout=float(p.get("TIMEOUT", 900)))
        out, rc = r.stdout + r.stderr, r.returncode
    except subprocess.TimeoutExpired as e:
        return t["name"], "TIMEOUT", ""
    if "AddressSanitizer" in out or "runtime error:" in out:
        return t["name"], "SANITIZER", out
    if p.get("DISABLED"):
        return t["name"], "DISABLED", ""
    skip = p.get("SKIP_REGULAR_EXPRESSION", [])
    if any(re.search(s, out) for s in skip):
        return t["name"], "SKIPPED", ""
    if any(re.search(f, out) for f in p.get("FAIL_REGULAR_EXPRESSION", [])):
        return t["name"], "FAIL-REGEX", out
    passes = p.get("PASS_REGULAR_EXPRESSION")
    ok = any(re.search(s, out) for s in passes) if passes else rc == 0
    if p.get("WILL_FAIL"):
        ok = not ok
    return t["name"], "PASS" if ok else f"FAIL rc={rc}", out


with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:
    results = list(ex.map(run, tests))
bad = 0
for name, verdict, out in results:
    print(f"{verdict:12s} {name}")
    if verdict not in ("PASS", "SKIPPED", "DISABLED"):
        bad += 1
        tail = "\n".join(out.splitlines()[-25:])
        print("    " + tail.replace("\n", "\n    "))
print(f"{len(results) - bad}/{len(results)} ok")
sys.exit(1 if bad else 0)
