# Range replay: formal models, fuzzing, stress and performance

Validation of the range replay PR (`cursor/rocprofv3-range-replay-a1b7`) at commit `9ded289d8a8`,
on one node with eight MI325X (gfx942) GPUs and ROCm 7.14. Everything here was run against that
build; the commands below reproduce it.

## What was found and fixed

| Commit | Bug | Found by |
|---|---|---|
| `b088bf4f6c8` | Host replay fenced through the intercepted doorbells while holding the per-agent writer locks: deadlock when another thread was dispatching. | TLA+ (`host_orig_*`), reproduced on hardware |
| `b088bf4f6c8` | Packet replay rang the application queue's doorbell while holding the writer lock; ROCr holds that queue's intercept lock while it calls the SDK interceptor, which waits for the reader lock. Deadlock with `GPU_MAX_HW_QUEUES=1`. | TLA+ (`packet_orig_*`), reproduced on hardware |
| `b088bf4f6c8` | Device-writing async copies took no lock and were never drained, so a copy from another thread that landed during a window was overwritten by the final restore. | TLA+ `NoLostWrite`, reproduced (16 of 35 writes lost) |
| `b088bf4f6c8` | A dispatch that waited out a window on the reader lock had published its decline into the previous range only, so the next range missed it. | TLA+ trace |
| `a24a228edd6` | `qualified_kernel_name` accumulated a 20-digit source-name length into `size_t`, wrapping `pos + len` past the bounds check; `_ZN18446744073709551615ncclDevKernel` was classified as a collective. | libFuzzer |
| `9ded289d8a8` | Docs: host replay can still decline after a cross-rank commit; tools must abort on a non-`REPLAYED` `CLOSE`. | TLA+ (`MultiRankReplay`, `host_agree_interf`) |

## Formal methods

### TLA+ (TLC 1.5.7)

`tla/ReplayWindowLocks.tla` models one range thread and one application thread that dispatches
and/or copies (`BOps`) on a queue it owns or shares with the range thread (`SharedQueue`), with
ROCr's intercept lock, the per-agent `shared_mutex`, the drain, the copy tickets and the final
restore. Invariant `NoLostWrite`; deadlock checking on. `Fixed` switches between the original
code and the fix.

| Configurations | Result |
|---|---|
| 12 original (`*_orig_*`): host/packet x own/shared queue x dispatch/copy/both | every one fails: 6 deadlock, 6 `NoLostWrite` violated |
| 12 fixed, `MaxB = 3` | no error (155 - 1,839 distinct states) |
| 12 fixed, `MaxB = 5` (`ReplayWindowLocksB5`) | no error (343 - 4,069 distinct states) |

`tla/MultiRankReplay.tla` models P ranks with a host collective inside the range, the commit
callback (`Agree`), asymmetric eligibility before the commit and interference after it.

| Configuration | Result |
|---|---|
| `host_noagree` | `NoMismatch` violated: why the commit callback exists |
| `host_agree` | no error |
| `host_agree_interf` | `NoMismatch` violated: a post-commit decline on one rank |
| `host_agree_interf_abort` | no error: the tool aborts on a lost agreement |
| `packet_noagree`, `packet_agree_interf` | no error |

Run: download `tla2tools.jar` 1.5.7 to `tools/`, then `tla/run.sh ReplayWindowLocks` (Java 8+).

The model covers one range thread per agent; two range threads on the same agent are not modeled.

### z3 (`z3/kernarg_layout.py`)

Proves the kernarg staging layout for all inputs (N < 2^24 dispatches, sizes < 2^32): every offset
is 256-aligned, no arithmetic wraps, each block holds its arguments, the loop invariant holds, and
`kernarg_block_fits` only reuses a block that holds the request and is at most 2x + 1 its size.
All 8 obligations PROVED.

The layout assumes no kernel's kernarg segment needs more than 256-byte alignment; the recorded
`kernarg_segment_alignment` is not consulted.

## Fuzzing and differential testing

`fuzz/gen_harness.py` extracts `qualified_kernel_name` verbatim from `range_state.cpp`, so the
harnesses test the shipped code.

- libFuzzer + ASan + UBSan: found the length overflow in 2 s (`fuzz/overflow-reproducer.bin`);
  after the fix, 394,759,251 runs in 600 s with no finding.
- Differential against `llvm-cxxfilt` on 329,212 `_Z` symbols from the ROCm 7.14 libraries: 0
  mismatches (162,259 exact or prefix matches, 120,879 intentionally empty, 46,061 operators or
  lambdas skipped).

## Stress and regression

| Check | Result |
|---|---|
| `test-range-replay-concurrency-*` (8 tests) on the pre-fix library | 4 packet tests deadlock (180 s timeout), 2 host copy tests lose 16/35 writes |
| same 8 tests, fixed, `--repeat until-fail:3` | pass |
| race soak, 4 configs x 8 iterations, 60 ranges | 32/32 pass, 0 lost writes |
| all 152 range-replay ctests, x2, then with `HSA_XNACK=1` | pass (1 PC-sampling test skipped as always) |
| XSBench/RSBench OpenMP offload matrix, XNACK 0 and 1 (`regress-x*.jsonl`) | 104/104 exit 0 with valid checksums |
| Jacobi and matmul ranges, packet and host | max abs diff 0 and 1.91e-6 (unchanged; matmul is FMA rounding) |
| MPI, 4 ranks, compute/host/rccl/rccl-after x packet/host x symmetric/one rank declines | 20/20 pass, every rank agrees, clean finalize |
| cooperative launch (`grid.sync`, 2,432 workgroups) in a range, packet and host | replayed, 0 mismatches |
| ASan: 24 range-replay integration tests and 4 samples | pass, no reports (ROCr-internal leaks at exit suppressed) |

The ASan unit-test binaries could not run: in that build they link both clang's static ASan runtime
and GCC's `libasan.so.8`. `asan_ctest.py` replays ctest commands without the duplicate preload.

Cooperative kernels expose an existing exit-time crash unrelated to range replay: ROCm 7.14's own
`rocprofv3 --kernel-trace` segfaults in `hsa_shut_down` (`AqlQueue::~AqlQueue`) on `coop/rr_coop`
with no range at all.

## Performance

| Measurement | Result |
|---|---|
| Copy tracking, 4 KiB H2D `hsa_amd_memory_async_copy`, submit-and-wait | 10.24 -> 10.57 us (+0.2 - 0.5 us) |
| same, 1 MiB | 29.13 -> 29.58 us (about +1.5%) |
| same, 4 KiB back-to-back | 10.6 -> 8.1 us (completion moves to an SDK signal) |
| Entry snapshot, XSBench large, 10.6 MB in 13 regions | 4.4 ms; drain 0.03 ms |
| `hsa_memory_copy` D2H, 256 MiB, pageable vs pinned | 56.1 vs 56.2 GB/s: pinning the snapshot buffers would not help |
| Replayed-pass time, 52 matrix rows, new / pre-fix | geomean 0.86; no row slower outside run-to-run noise |

Three XNACK=1 XSBench-large single-kernel rows looked 6 - 7x slower than an older run (35 ms -> 260
ms per pass). An A/B on the same harness gave 243 - 292 ms on the pre-fix library and 239 - 300 ms
on the fixed one (`ab-xnack-*.jsonl`): an environment difference from that older run, not the fix.
