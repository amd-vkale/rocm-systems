"""Proves plan_kernarg_layout (range_replay/executor.cpp) and kernarg_block_fits
(range_replay/retained_kernarg.hpp) correct over 64-bit size_t arithmetic.

plan_kernarg_layout:
    offsets[i] = total_i
    total_{i+1} = total_i + ceil(size_i / 256) * 256

Inductive invariant, for every prefix of i dispatches (0 <= i <= N) with each kernarg size
below 2^32 (a code object stores the kernarg segment size as uint32):

    I(i):  total_i % 256 == 0  /\\  total_i <= i * (2^32 + 255)

Step obligations, for dispatch i with size s:
    1. offset_i = total_i is 256-aligned (each argument block starts aligned);
    2. offset_i + s <= total_{i+1} (the block holds the arguments);
    3. no wraparound anywhere, for N up to 2^24 dispatches;
    4. I(i+1);
and consecutive blocks never overlap because offset_{i+1} = total_{i+1} >= offset_i + s.

kernarg_block_fits(capacity, request) = capacity >= request && capacity / 2 <= request:
    5. a reused block holds the request;
    6. it is never more than twice (plus one byte) the request, so retention is bounded;
    7. a freshly allocated block of exactly `request` bytes is reused by the same request.
"""
import sys

from z3 import (UGE, ULE, ULT, BitVec, BitVecVal, Implies, Not, Solver, URem, UDiv, And,
                ZeroExt, unsat)

W = 64
ALIGN = BitVecVal(256, W)
MAX_SIZE = BitVecVal(2**32 - 1, W)
MAX_N = 2**24


def prove(name, claim):
    s = Solver()
    s.add(Not(claim))
    r = s.check()
    ok = r == unsat
    print(f"{'PROVED' if ok else 'FAILED'}  {name}")
    if not ok:
        print("   counterexample:", s.model())
    return ok


def round_up(size):
    return UDiv(size + ALIGN - 1, ALIGN) * ALIGN


i = BitVec("i", W)
total = BitVec("total", W)
size = BitVec("size", W)

# Wide (128-bit) shadows detect any 64-bit wraparound.
def wide(x):
    return ZeroExt(64, x)

bound_i = (2**32 + 255)
inv = And(URem(total, ALIGN) == 0, ULE(wide(total), wide(i) * (2**32 + 255)))
pre = And(ULT(i, MAX_N), ULE(size, MAX_SIZE), inv)
new_total = total + round_up(size)
wide_new_total = wide(total) + (wide(size) + 255) / 256 * 256

ok = True
ok &= prove("offset is 256-aligned", Implies(pre, URem(total, ALIGN) == 0))
ok &= prove("no wraparound in round_up or the sum",
            Implies(pre, wide(new_total) == wide_new_total))
ok &= prove("block holds the arguments: offset + size <= next total",
            Implies(pre, ULE(wide(total) + wide(size), wide(new_total))))
ok &= prove("invariant preserved",
            Implies(pre, And(URem(new_total, ALIGN) == 0,
                             ULE(wide(new_total), (wide(i) + 1) * bound_i))))
ok &= prove("invariant holds initially (i = 0, total = 0)",
            Implies(And(i == 0, total == 0), inv))

cap = BitVec("capacity", W)
req = BitVec("request", W)
fits = And(UGE(cap, req), ULE(UDiv(cap, 2), req))
ok &= prove("reused block holds the request", Implies(fits, UGE(cap, req)))
ok &= prove("reused block is at most 2 * request + 1",
            Implies(fits, ULE(wide(cap), 2 * wide(req) + 1)))
ok &= prove("a block allocated for the request fits it", Implies(cap == req, fits))

sys.exit(0 if ok else 1)
