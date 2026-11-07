"""Differential check of qualified_kernel_name() against llvm-cxxfilt on real mangled symbols.

Property: when qualified_kernel_name(m) is non-empty and m is a _Z name, its "::"-separated
components equal the first k components of the demangled qualified function name (with template
argument lists removed and "(anonymous namespace)" spelled _GLOBAL__N_1), for some k >= 1.
Names whose demangling the simple parser below can't split reliably (operators, lambdas, local
entities) are skipped and counted."""
import collections
import re
import subprocess
import sys

CXXFILT = "/opt/rocm/llvm/bin/llvm-cxxfilt"


def split_top(s, sep):
    out, depth, cur, i = [], 0, "", 0
    while i < len(s):
        c = s[i]
        if c in "<(":
            depth += 1
        elif c in ">)":
            depth -= 1
        if depth == 0 and s.startswith(sep, i):
            out.append(cur)
            cur = ""
            i += len(sep)
            continue
        cur += c
        i += 1
    out.append(cur)
    return out


def strip_templates(s):
    out, depth = "", 0
    for c in s:
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        elif depth == 0:
            out += c
    return out


def function_components(demangled):
    if any(t in demangled for t in ("operator", "{lambda", "'", "{unnamed", "::{", "decltype")):
        return None
    depth, cut = 0, None
    for i, c in enumerate(demangled):
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        elif c == "(" and depth == 0:
            cut = i
            break
    name = demangled if cut is None else demangled[:cut]
    name = split_top(name, " ")[-1]
    name = name.replace("(anonymous namespace)", "_GLOBAL__N_1")
    name = re.sub(r"\[abi:[^\]]*\]", "", name)
    return [strip_templates(p) for p in split_top(name, "::")]


def main(names_file):
    mangled = [l.strip() for l in open(names_file) if l.strip().startswith("_Z")]
    mangled = sorted(set(m.split("@")[0] for m in mangled))
    ours = subprocess.run(["./qkn_diff"], input="\n".join(mangled) + "\n", capture_output=True,
                          text=True, check=True).stdout.splitlines()
    dem = subprocess.run([CXXFILT], input="\n".join(mangled) + "\n", capture_output=True,
                         text=True, check=True).stdout.splitlines()
    assert len(ours) == len(dem) == len(mangled)
    stats = collections.Counter()
    bad = []
    for m, line, d in zip(mangled, ours, dem):
        q = line.split("\t", 1)[1]
        if d == m:
            stats["not-demangled"] += 1
            continue
        comps = function_components(d)
        if comps is None:
            stats["skipped"] += 1
            continue
        if not q:
            stats["empty"] += 1
            continue
        qc = q.split("::")
        if qc == comps[:len(qc)]:
            stats["prefix-match" if len(qc) < len(comps) else "exact-match"] += 1
        else:
            stats["MISMATCH"] += 1
            bad.append((m, q, d))
    print(dict(stats), "total", len(mangled))
    for b in bad[:20]:
        print("MISMATCH", *b, sep="\n  ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
