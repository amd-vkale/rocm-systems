#!/usr/bin/env python3
"""Aggregate results/*/results.jsonl into markdown tables."""

import glob
import json
import os
import statistics
import sys
from collections import defaultdict

ROOT = os.environ.get("WORK", os.path.join(os.path.dirname(os.path.abspath(__file__)), "work"))


def load(pattern):
    rows = []
    for f in sorted(glob.glob(os.path.join(ROOT, "results", pattern, "results.jsonl"))):
        with open(f) as fh:
            rows += [json.loads(l) for l in fh if l.strip()]
    return rows


def close_of(r):
    for c in r["close"]:
        if "status" in c:
            return c
    return {}


def fini_of(r):
    for c in r["close"]:
        if "late_records" in c:
            return c
    return {}


def fmt_ms(v):
    return "-" if v is None else f"{v:.0f}"


def status_table(rows):
    out = ["| app | size | config | verify | status | dispatches | divergence | checksum | "
           "app check | pass 0 ms | replayed pass ms (mean) | last leg ms |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        c = close_of(r)
        pm = c.get("pass_ms", [])
        mid = statistics.mean(pm[1:-1]) if len(pm) > 2 else None
        out.append("| {app} | {size} | {cfg} | {v} | {st} | {d} | {div} | {ck} | {ok} | {p0} | {mid} | {last} |".format(
            app=r["app"], size=r["size"], cfg=r["config"], v=int(r["verify"]),
            st=c.get("status", "-"), d=c.get("dispatch_count", "-"),
            div=c.get("divergence_count", "-"), ck=r["checksum"],
            ok="pass" if r["valid"] else "**FAIL**",
            p0=fmt_ms(pm[0]) if pm else "-", mid=fmt_ms(mid),
            last=fmt_ms(pm[-1]) if len(pm) > 1 else "-"))
    return "\n".join(out)


def counter_table(rows):
    grp = defaultdict(list)
    for r in rows:
        grp[(r["app"], r["size"], r["config"])].append(r)
    out = ["| app | size | config | runs | passes with SQ_WAVES != 4864 | runs missing a pass at CLOSE | "
           "late records (after CLOSE) |",
           "|---|---|---|---|---|---|---|"]
    for (app, size, cfg), rs in sorted(grp.items()):
        wrong = total = missing = late = 0
        for r in rs:
            c = close_of(r)
            cs = c.get("counters", [])
            delivered = [x for x in cs if x["records"] > 0]
            total += len(delivered)
            wrong += sum(1 for x in delivered if int(x["sum"]) != 4864)
            if len(delivered) < r["passes"]:
                missing += 1
            late += sum(x["records"] for x in fini_of(r).get("late_records", []))
        out.append(f"| {app} | {size} | {cfg} | {len(rs)} | {wrong} / {total} | {missing} / {len(rs)} | {late} |")
    return "\n".join(out)


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "status"
    pattern = sys.argv[2] if len(sys.argv) > 2 else "x0-*"
    rows = load(pattern)
    print(status_table(rows) if which == "status" else counter_table(rows))


if __name__ == "__main__":
    main()
