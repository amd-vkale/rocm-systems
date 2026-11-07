#!/usr/bin/env python3
"""Summarize results-v1v2/results.jsonl into the v1-vs-v2 tables (markdown on stdout)."""

import collections
import json
import os
import statistics
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "results-v1v2", "results.jsonl")
rows = [json.loads(l) for l in open(path) if l.strip()]
for r in rows:
    r["close"] = [c for c in r["close"] if "status" in c]

CFG_ORDER = ["native", "v1-packet", "v2-packet", "v2-host", "v2-host-notrack", "v1-host",
             "native-nomm", "v1-packet-nomm", "v2-packet-nomm", "v2-host-nomm"]
groups = collections.defaultdict(list)
for r in rows:
    groups[(r["bench"], r["range"], r["config"])].append(r)

native_hash = {}
native_ms = {}
for (b, rng, cfg), rs in groups.items():
    if cfg == "native":
        hs = {r["hash"] for r in rs if r["hash"]}
        native_hash[(b, rng)] = hs
        ms = [r["app_range_ms"] for r in rs if r["app_range_ms"] is not None]
        if not ms:
            ms = [r.get("app_runtime_s", 0) * 1e3 for r in rs if r.get("app_runtime_s")]
        native_ms[(b, rng)] = statistics.median(ms) if ms else None


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def fmt(x, nd=1):
    return "-" if x is None else f"{x:.{nd}f}"


def summarize(b, rng, cfg, rs):
    n = len(rs)
    ok = sum(1 for r in rs if r["exit"] == 0 and r["valid"])
    nh = native_hash.get((b, rng), set())
    hash_ok = sum(1 for r in rs if r["hash"] and r["hash"] in nh)
    has_hash = any(r["hash"] for r in rs)
    statuses = collections.Counter(
        (r["close"][0]["status"] if r["close"] else ("NO-SDK-HOST" if r["exit"] == 2 else "-"))
        for r in rs)
    disp = sorted({r["close"][0]["dispatch_count"] for r in rs if r["close"]})
    app_ms = med([r["app_range_ms"] for r in rs])
    if app_ms is None:
        app_ms = med([r.get("app_runtime_s") and r["app_runtime_s"] * 1e3 for r in rs])
    p0 = med([r["close"][0]["pass_ms"][0] for r in rs if r["close"] and r["close"][0]["pass_ms"]])
    rp = med([statistics.mean(r["close"][0]["pass_ms"][1:]) for r in rs
              if r["close"] and len(r["close"][0]["pass_ms"]) > 1])
    ctr_consistent = None
    ctr_rows = [r for r in rs if r["close"] and r["close"][0]["status"] == "REPLAYED"]
    if ctr_rows:
        ctr_consistent = all(
            len({c["sum"] for c in r["close"][0]["counters"]}) == 1
            and all(c["records"] == c["dispatches"] for c in r["close"][0]["counters"])
            for r in ctr_rows)
    ctr_sum = sorted({r["close"][0]["counters"][0]["sum"] for r in ctr_rows
                      if r["close"][0]["counters"]})
    base = native_ms.get((b, rng))
    return {
        "n": n, "ok": ok, "hash_ok": hash_ok if has_hash else None, "statuses": dict(statuses),
        "dispatches": disp, "app_ms": app_ms, "overhead": (app_ms / base) if (app_ms and base) else None,
        "pass0_ms": p0, "replay_pass_ms": rp, "ctr_consistent": ctr_consistent, "ctr_sum": ctr_sum,
    }


summary = {}
print("| bench | range | config | valid | hash=native | status | dispatches | range ms (median) | x native | pass0 ms | replay pass ms | SQ_WAVES per pass equal |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|")
for (b, rng) in sorted({(k[0], k[1]) for k in groups}):
    for cfg in CFG_ORDER:
        rs = groups.get((b, rng, cfg))
        if not rs:
            continue
        s = summarize(b, rng, cfg, rs)
        summary[f"{b}|{rng}|{cfg}"] = s
        st = ", ".join(f"{k} x{v}" for k, v in s["statuses"].items())
        hok = "-" if s["hash_ok"] is None else f"{s['hash_ok']}/{s['n']}"
        cc = "-" if s["ctr_consistent"] is None else ("yes" if s["ctr_consistent"] else "NO")
        print(f"| {b} | {rng} | {cfg} | {s['ok']}/{s['n']} | {hok} | {st} | "
              f"{','.join(map(str, s['dispatches'])) or '-'} | {fmt(s['app_ms'])} | "
              f"{fmt(s['overhead'], 2)} | {fmt(s['pass0_ms'])} | {fmt(s['replay_pass_ms'])} | {cc} |")

# v1 vs v2 dispatch count and counter agreement on the same benchmark and range.
print()
print("| bench | range | v1 packet dispatches | v2 packet dispatches | v2 host dispatches | SQ_WAVES v1 / v2 packet / v2 host |")
print("|---|---|---|---|---|---|")
for (b, rng) in sorted({(k[0], k[1]) for k in groups}):
    g = lambda c: summary.get(f"{b}|{rng}|{c}")
    v1, v2, v2h = g("v1-packet"), g("v2-packet"), g("v2-host")
    if not (v1 and v2):
        continue
    print(f"| {b} | {rng} | {v1['dispatches']} | {v2['dispatches']} | {v2h['dispatches'] if v2h else '-'} | "
          f"{v1['ctr_sum']} / {v2['ctr_sum']} / {v2h['ctr_sum'] if v2h else '-'} |")


def cell(s):
    if s is None:
        return "-"
    st = max(s["statuses"], key=s["statuses"].get)
    st = {"REPLAYED": "", "MEMORY_COPY_IN_RANGE": " copy-decl",
          "ALLOCATION_CHANGED_IN_RANGE": " alloc-decl", "-": ""}.get(st, " " + st)
    bad = "" if s["ok"] == s["n"] else f" INVALID {s['n'] - s['ok']}/{s['n']}"
    return f"{fmt(s['app_ms'])}{st}{bad}"


print()
print("Condensed: median range wall ms (app-measured, all passes); copy-decl / alloc-decl = declined,"
      " ran once")
print()
print("| bench | range | native | v1 packet | v2 packet | v2 host | v2 host, pool off |")
print("|---|---|---|---|---|---|---|")
for (b, rng) in sorted({(k[0], k[1]) for k in groups}):
    g = lambda c: summary.get(f"{b}|{rng}|{c}")
    print(f"| {b} | {rng} | {cell(g('native'))} | {cell(g('v1-packet'))} | {cell(g('v2-packet'))} | "
          f"{cell(g('v2-host'))} | {cell(g('v2-host-nomm'))} |")

# First replayed pass vs steady state: v2 packet creates its private replay queue on first use.
print()
print("| bench | range | v1 packet pass1 / pass2 ms | v2 packet pass1 / pass2 ms | v2 host pass1 / pass2 ms |")
print("|---|---|---|---|---|")
for (b, rng) in sorted({(k[0], k[1]) for k in groups}):
    out = []
    for c in ("v1-packet", "v2-packet", "v2-host"):
        rs = [r for r in groups.get((b, rng, c), [])
              if r["close"] and r["close"][0]["status"] == "REPLAYED" and len(r["close"][0]["pass_ms"]) >= 3]
        if rs:
            p1 = med([r["close"][0]["pass_ms"][1] for r in rs])
            p2 = med([r["close"][0]["pass_ms"][2] for r in rs])
            out.append(f"{p1:.1f} / {p2:.1f}")
        else:
            out.append("-")
    if any(o != "-" for o in out):
        print(f"| {b} | {rng} | " + " | ".join(out) + " |")

with open(os.path.join(os.path.dirname(path), "summary.json"), "w") as f:
    json.dump(summary, f, indent=1, sort_keys=True)
