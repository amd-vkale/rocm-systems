# Range replay on XSBench and RSBench (OpenMP offload), Banff MI325X

Hard results for range replay (packet and host modes) on the OpenMP offload ports of XSBench and
RSBench, run on Banff on Nov 6, 2026 (PT). This covers the same application set as the
correctness matrix in [#14030](https://github.com/ROCm/rocm-systems/pull/14030) (HIP and OpenMP
builds of BabelStream, XSBench, RSBench and miniBUDE), restricted to the OpenMP offload builds of
XSBench and RSBench, and adds host range replay from
[#15289](https://github.com/ROCm/rocm-systems/pull/15289).

## Summary

- **Correctness.** 508 recorded application runs (460 in `results/*.jsonl`, plus 48 in the
  `SQ_WAVES` control). Every run that the unmodified application passes also passes with range
  replay on: same verification checksum, in packet and host mode, with and without the divergence
  check. The only failing cell is XSBench large with XNACK on, and the
  unmodified binary fails it too (checksum 0).
- **What replays.**
  - A kernel-only range (data mapped with `target enter data` before the range) replays in packet
    mode with divergence 0.
  - Host mode replays the same range once libomptarget's caching memory manager is disabled
    (`LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD=0`). With the memory manager on, it declines
    `ALLOCATION_CHANGED_IN_RANGE`.
  - The two modes need opposite settings: with the memory manager off, packet replay declines the
    same range.
  - A range around the whole simulation, including its `map` copies and host reduction, can only
    be host-replayed. It replays with divergence 0 once the OpenMP image is warmed up before the
    range.
- **Four problems found:**
  1. A host range that is the first OpenMP offload of the process replays with
     `divergence_count=1`: libomptarget's device-allocator init is undone by the restore.
  2. In host replay of a kernel-only range, the final pass's counter record is delivered after
     CLOSE, against the documented contract.
  3. Under XNACK zero-copy, ranges replay with divergence 0 although the mapped data is not
     snapshotted or checked.
  4. One unexplained 314 s pass-0 stall, which did not reproduce in 20 further runs.
- **Cost.** For a kernel-only range, the snapshot dominates. Host replay is 1.7x cheaper than packet
  replay on XSBench large (3.2 s against 5.7 s for 4 passes of a 6.07 GB working set), because it
  takes no exit snapshot. A full-range host replay snapshots only 10.6 MB, so each extra pass costs
  about what the application's own run costs.
- **SDK tests.** The range replay CTests pass on MI325X: 126 passed, 1 skipped, 0 failed.

## About Minseob's `validate.md`

I could not find a `validate.md` from Minseob. I checked the Banff filesystem, all 5,541 local
branches of `rocm-systems` (including all 26 `users/mishin/*` branches), PRs #10864 and #15289,
and Minseob's public GitHub repositories. The closest match is the test plan and results of
[#14030](https://github.com/ROCm/rocm-systems/pull/14030), which names the same applications. If
the file is somewhere else, the comparison below should be re-checked against it.

## Setup

| Item | Value |
|---|---|
| Machine | `banff-ccs-aus-g05-05`, 8x AMD Instinct MI325X (gfx942, `sramecc+:xnack+`), 224 cores, 2 TB RAM |
| ROCm | 7.14.0 (`/opt/rocm-7.14.0`), AMD clang 23.0.0git |
| rocprofiler-sdk | PR #15289 head `219a133ccad` (`cursor/range-replay-host-setjmp-fa02`), RelWithDebInfo |
| XSBench | `ba08e52`, `openmp-offload`, `-m event`, `-s small` / `-s large` |
| RSBench | `34b6447`, `openmp-offload`, `-m event`, `-s small` / `-s large` |
| Build | `amdclang -fopenmp --offload-arch=gfx942 -O3` |
| Passes | 4 (the application's own run plus 3 replays), one GPU per process |

Each process ran on one GPU (`ROCR_VISIBLE_DEVICES`). The correctness matrices ran four processes
at once on four GPUs. The timing table ran one process on the whole machine.

## How the ranges were placed

`rr_app.h` puts two ranges into each app, selected with `RR_RANGE`:

- **kernel**: the inputs and the output array are mapped with `target enter data` before the range.
  The range then holds exactly one dispatch (the `target teams distribute parallel for`) and no
  copies. The mapping is released with `target exit data` after the range.
- **full**: the range wraps the call to `run_event_based_simulation`: the `map(to:)` /
  `map(from:)` allocations and copies, the kernel, and the host-side hash reduction. In host mode
  each pass recomputes and prints the hash (`[rr-app] ... host_pass=N hash=...`); every pass
  printed the correct hash in every run.

`RR_APP_MODE=packet` opens the range with `rocprofiler_range_replay_begin`. `RR_APP_MODE=host`
takes the `sigsetjmp` and opens it the way `ROCPROFILER_RANGE_REPLAY_HOST_BEGIN` does. The apps
resolve these entry points at run time (`rr_rt.h`) rather than linking the SDK, so one binary runs
against range replay v1, v2, or no SDK; `run_matrix.py` preloads the SDK under test. The tool (`rr_tool.cpp`, `LD_PRELOAD` through
`ROCP_TOOL_LIBRARIES`) sets the pass count, sets `host_replay` from `RR_HOST_REPLAY`, times each
pass, optionally collects a different counter on each pass (`RR_COUNTERS`), and writes the CLOSE
record as JSON.

Configurations (from `run_matrix.py`):

| config | range | mode | extra |
|---|---|---|---|
| `pristine` | none | none | unmodified upstream binary, no SDK |
| `instr-notool` | none | none | instrumented binary, no tool |
| `kernel-packet` | kernel | packet | |
| `kernel-host` | kernel | host | |
| `kernel-host-nomm` | kernel | host | `LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD=0` |
| `full-packet` | full | packet | |
| `full-host` | full | host | |
| `full-host-nomm` | full | host | memory manager off |
| `full-host-nomm-warm` | full | host | memory manager off, one empty `target` region before the range |
| `kernel-host-unarmed` | kernel | tool asks for host, app opens with plain `begin` | negative test |
| `*-ctr` | | | `RR_COUNTERS=SQ_WAVES,SQ_INSTS_VALU,SQ_INSTS_SALU,SQ_INSTS_VMEM_RD`, one per pass |

## Correctness: XNACK off (`HSA_XNACK=0`)

Each cell was run with the divergence check off and on (`ROCPROF_RANGE_REPLAY_VERIFY=1`). The
status was the same both ways. The divergence count shown is from the run with the check on. Every
cell passed the application's own checksum (XSBench small 945990, XSBench large 952131, RSBench
small 880018, RSBench large 358389).

| config | xsbench small | xsbench large | rsbench small | rsbench large |
|---|---|---|---|---|
| kernel-packet | REPLAYED, div 0 | REPLAYED, div 0 | REPLAYED, div 0 | REPLAYED, div 0 |
| kernel-host | ALLOCATION_CHANGED | ALLOCATION_CHANGED | ALLOCATION_CHANGED | ALLOCATION_CHANGED |
| kernel-host-nomm | REPLAYED, div 0 | REPLAYED, div 0 | REPLAYED, div 0 | REPLAYED, div 0 |
| full-packet | MEMORY_COPY_IN_RANGE | MEMORY_COPY_IN_RANGE | MEMORY_COPY_IN_RANGE | MEMORY_COPY_IN_RANGE |
| full-host | ALLOCATION_CHANGED | ALLOCATION_CHANGED | ALLOCATION_CHANGED | ALLOCATION_CHANGED |
| full-host-nomm | REPLAYED, **div 1** | REPLAYED, **div 1** | REPLAYED, **div 1** | REPLAYED, **div 1** |
| full-host-nomm-warm | REPLAYED, div 0 | REPLAYED, div 0 | REPLAYED, div 0 | REPLAYED, div 0 |
| kernel-host-unarmed | HOST_JMP_NOT_SET | HOST_JMP_NOT_SET | HOST_JMP_NOT_SET | HOST_JMP_NOT_SET |

(`ALLOCATION_CHANGED` is `ALLOCATION_CHANGED_IN_RANGE`.)

- `full-packet` declines as designed: the `map` clauses copy inside the range.
- `kernel-host` and `full-host` decline because libomptarget's memory manager keeps a per-launch
  device allocation cached across the end of the pass, so the tracked allocation set differs.
  With the memory manager off (`*-nomm`), the allocation is freed in the same pass and the range
  replays.
- `kernel-host-unarmed` declines `HOST_JMP_NOT_SET` as documented.
- The memory manager setting makes the two modes mutually exclusive for a kernel range. With
  `LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD=0`, `kernel-packet` declines `ALLOCATION_CHANGED_IN_RANGE`
  instead (XSBench and RSBench small, 13 of 13 runs). Packet replay refuses any allocation inside
  the range, even one freed in the same pass. Host replay refuses an allocation that is still live
  at the end of the pass, which is what the caching memory manager leaves behind. No single
  libomptarget setting lets both modes replay the same OpenMP kernel range.

## Correctness: XNACK on, OpenMP zero-copy (`HSA_XNACK=1 OMPX_APU_MAPS=1`)

| config | xsbench small | xsbench large | rsbench small | rsbench large |
|---|---|---|---|---|
| pristine | pass | **FAIL (checksum 0)** | pass | pass |
| kernel-packet | REPLAYED, div 0 | (app fails without SDK) | REPLAYED, div 0 | REPLAYED, div 0 |
| kernel-host | REPLAYED, div 0 | | REPLAYED, div 0 | REPLAYED, div 0 |
| kernel-host-nomm | REPLAYED, div 0 | | REPLAYED, div 0 | REPLAYED, div 0 |
| full-packet | MEMORY_COPY_IN_RANGE | | MEMORY_COPY_IN_RANGE | MEMORY_COPY_IN_RANGE |
| full-host | REPLAYED, **div 1** | | REPLAYED, **div 1** | REPLAYED, **div 1** |
| full-host-nomm | REPLAYED, **div 1** | | REPLAYED, **div 1** | REPLAYED, **div 1** |
| full-host-nomm-warm | REPLAYED, div 0 | | REPLAYED, div 0 | REPLAYED, div 0 |
| kernel-host-unarmed | HOST_JMP_NOT_SET | | HOST_JMP_NOT_SET | HOST_JMP_NOT_SET |

- XSBench large under zero-copy fails its own checksum without the SDK, so that column says
  nothing about replay.
- `kernel-host` replays here even with the memory manager on. Zero-copy maps make no device
  allocation, so the allocation set does not change.
- Every other cell passed the application's own checksum. That is only because these kernels are
  idempotent (finding 3).
- Timings with XNACK on are not comparable to anything else. Four processes ran at once, and
  first-touch page faults dominate pass 0 (XSBench small: about 30 s for pass 0, against 275 ms
  for each replayed pass).

## Findings

### 1. A host range that is the process's first OpenMP offload replays with divergence 1

`full-host-nomm` reports `REPLAYED` with `divergence_count=1` in 4 of 4 apps and sizes with XNACK
off, and in all of them with XNACK on. The application's checksum is still correct.

A local diagnostic (`patches/sdk-divergence-diag.patch`, gated by `ROCPROF_RANGE_REPLAY_DIAG=1`)
logs each divergent block. In every case it is one 10,616,832-byte tracked allocation that existed
before the range:

```text
range replay diag: agent ... blocks=13 exit_digests=13 final_digests=13 entry_blocks=1
range replay diag: divergent block 0 addr=0x7f9906000000 bytes=10616832 from_tracker=true in_entry_snapshot=true
```

The cause is in the pass-0 dispatches. `full-host-nomm` reports `dispatch_count=2`, and
`rocprofv3 --pmc SQ_WAVES` on the unmodified binary shows the extra one is `__omp_dm_init_kernel`.
libomptarget launches it once per process, the first time a target region runs, to initialize its
device-memory allocator heap (the 10.6 MB block).

1. In pass 0, the init kernel runs inside the range.
2. The restore before pass 1 puts the heap back to its pre-init contents.
3. libomptarget believes it is initialized and never relaunches the init kernel.
4. Passes 1 to 3 therefore run with an uninitialized allocator heap.

These kernels do not call device `malloc`, so their results are unaffected. A kernel that did would
see a corrupt heap. The divergence check caught this correctly.

With one empty `#pragma omp target {}` before the range (`full-host-nomm-warm`), the dispatch count
is 1 and the divergence count is 0 everywhere. The host replay doc already says to launch each
kernel once before the range. This case is worth calling out explicitly for OpenMP, because the
first target region triggers runtime initialization, not just a code-object load. Packet replay
declines this range anyway (`MEMORY_COPY_IN_RANGE`).

### 2. Host replay of a kernel range delivers the final pass's counter record after CLOSE

`finish_pass` documents that "the pass's GPU tail belongs to the pass: its records are emitted
before PASS exit". With per-pass counters in a host-replayed kernel range, the last pass's
dispatch-counter record is often delivered after the CLOSE callback.

The tool counts a record as late when it arrives after CLOSE (8 runs per cell, `results/late-records.jsonl`):

| app | size | config | runs with the last pass's record after CLOSE |
|---|---|---|---|
| rsbench | small | kernel-host-nomm-ctr | **8 / 8** |
| rsbench | large | kernel-host-nomm-ctr | **3 / 8** |
| rsbench | small, large | kernel-packet-ctr | 0 / 16 |
| rsbench | small, large | full-host-nomm-warm-ctr | 0 / 16 |
| xsbench | small, large | all three | 0 / 48 |

The record does arrive later, so a total count at finalize looks fine, which is what
`tests/range-replay-host` checks. A tool that finalizes its per-range data at CLOSE loses the last
pass.

It only happens when `rocprofiler_range_replay_end()` follows the kernel immediately. In the full
range, the device-to-host copy and the host reduction after the kernel give the completion handler
time to run. `fence_all` waits for the GPU but not for the SDK's completion handler that emits the
record.

Repro: `RR_MATRIX_COUNTERS=SQ_WAVES python3 run_matrix.py --apps rsbench --sizes small --verify 0 --reps 8 --configs kernel-host-nomm-ctr`.

### 3. XNACK zero-copy ranges replay unguarded

With `HSA_XNACK=1 OMPX_APU_MAPS=1`, the mapped arrays stay in host memory. The snapshot does not
capture them and the divergence check does not hash them. Every kernel-range run reports
`REPLAYED` with divergence 0. XSBench and RSBench only produce correct results because their kernels
are idempotent: they write `verification[i] = f(i)` from read-only inputs.

This is the case the untracked-memory guard in
[#14030](https://github.com/ROCm/rocm-systems/pull/14030) declines ("OpenMP zero-copy under XNACK:
replays, application check fails"). That guard is not in the #15289 branch.

### 4. One 314 s pass-0 stall (not reproduced)

One `xsbench-large-kernel-host-nomm` run in the timing set spent 313,830 ms in pass 0. The other
four identical runs took about 2,340 ms. It also spent about 244 s outside the range, in the
`target enter data` copy. Replayed passes ran at the normal 174 ms, and the result was valid.

While it was stalled:
- The main thread was spinning in ROCr's `InterruptSignal::WaitRelaxed` (perf; ptrace is blocked
  on Banff, so no deeper stack).
- ROCr's async-events thread was spinning too.
- GPU 0 was idle.

So bulk host-device copies stalled, both the SDK's snapshot copy and the application's own map
copy. The root filesystem was at 100% during the session, and the box is shared. The cause was not
determined.

It did not reproduce. In 20 further runs on the same GPU (`results/stall-repro.jsonl`), all
results were valid:
- 10 `kernel-host-nomm` runs: 3.12 to 3.30 s.
- 10 `kernel-packet` runs: 5.51 to 5.63 s.

### 5. `SQ_WAVES` over-counts, independent of range replay

The true value is 4864 for both simulation kernels: 1216 workgroups x 4 waves, confirmed 3 of 3
times with `rocprofv3 --pmc SQ_WAVES` on the unmodified binary.

Under replay, single passes sometimes read 7296 to 35248. That happened in 11 of 36 runs when four
processes ran on four GPUs, and in 3 of about 300 passes when one process ran alone. The same tool
on the same binary with no range (`control_sqwaves.sh`) also read 12160 once in 24 runs. The
over-count is in counter collection on this machine, not in range replay, though it shows up more
often during replay. VALU, SALU and VMEM read counts per pass agree between packet and host replay
to 3 or 4 significant digits.

## Per-pass counters

Collecting a different counter on each pass works in both modes. For example, RSBench large,
4 passes, each pass collecting a different counter:

| mode | pass 0 SQ_WAVES | pass 1 SQ_INSTS_VALU | pass 2 SQ_INSTS_SALU | pass 3 SQ_INSTS_VMEM_RD |
|---|---|---|---|---|
| kernel, packet | 4864 (in clean runs) | 1.147e11 | 1.236e10 | 3.401e9 |
| kernel, host | 4864 (in clean runs) | 1.147e11 | 1.235e10 | (after CLOSE, finding 2) |
| full, host (warm) | 4864 | 1.147e11 | 1.234e10 | 3.400e9 |

## Cost

Single process on the machine, divergence check off, 4 passes, 5 runs per row, medians. Pass 0 is
the time from CONFIG to the first PASS callback. It includes the entry snapshot, plus the exit
snapshot in packet mode. The last leg is from the last PASS callback to CLOSE.

| app | size | config | runtime s | pass 0 ms | replayed pass ms | last leg ms |
|---|---|---|---|---|---|---|
| xsbench | small | pristine | 0.102 | - | - | - |
| xsbench | small | kernel-packet | 0.522 | 325 | 26 | 44 |
| xsbench | small | kernel-host-nomm | 0.345 | 179 | 26 | 17 |
| xsbench | small | full-host-nomm-warm | 0.291 | 83 | 62 | 62 |
| xsbench | large | pristine | 0.314 | - | - | - |
| xsbench | large | kernel-packet | 5.663 | 4487 | 174 | 291 |
| xsbench | large | kernel-host-nomm | 3.246 | 2338 | 174 | 48 |
| xsbench | large | full-host-nomm-warm | 1.295 | 282 | 266 | 251 |
| rsbench | small | pristine | 0.091 | - | - | - |
| rsbench | small | kernel-packet | 0.374 | 167 | 49 | 54 |
| rsbench | small | kernel-host-nomm | 0.273 | 88 | 49 | 46 |
| rsbench | small | full-host-nomm-warm | 0.311 | 79 | 73 | 72 |
| rsbench | large | pristine | 0.388 | - | - | - |
| rsbench | large | kernel-packet | 1.526 | 437 | 342 | 348 |
| rsbench | large | kernel-host-nomm | 1.517 | 393 | 343 | 340 |
| rsbench | large | full-host-nomm-warm | 1.508 | 387 | 365 | 364 |

The instrumented binary without a tool costs nothing measurable (`instr-notool` is within run-to-run
noise of `pristine`; see `results/timing.jsonl`).

Entry snapshot size and time, from the SDK's INFO log:

| app | size | kernel range | full range (host) |
|---|---|---|---|
| xsbench | small | 399 MB, 143 ms | 10.6 MB, 4.5 ms |
| xsbench | large | 6.07 GB, 2,126 to 2,129 ms | 10.6 MB, 4.4 ms |
| rsbench | small | 98 MB, 34 to 53 ms | 10.6 MB, 4.4 ms |
| rsbench | large | 122 MB, 43 ms | 10.6 MB, 6.4 ms |

- Snapshot copies run at about 2.8 GB/s into pageable host memory.
- On XSBench large, packet replay pays for an exit snapshot as well as the entry snapshot (pass 0:
  4.5 s against 2.3 s for host replay).
- The divergence check adds another 9 to 15 s of host-side hashing of the 6 GB snapshot
  (`results/x0-xsbench-large.jsonl`, `verify: true`). That is the cost
  [#14808](https://github.com/ROCm/rocm-systems/pull/14808) targets.
- In a full range, the data is mapped inside the range, so the snapshot holds only the libomptarget
  heap. Each replayed pass then costs about what the application's own run costs, copies included.

## SDK range replay CTests on MI325X

`ctest -R "range.replay|range-replay"` in the #15289 build on GPUs 4 to 7: 127 tests, 126 passed,
0 failed, 1 skipped (`test-range-replay-local-context-pc-sampling-stop-after-0`). The log is
`results/ctest-range-replay.log`.

## Reproduce

```bash
# rocprofiler-sdk build of this branch (or #15289) in $SDK_BUILD
export SDK_BUILD=/path/to/build-sdk ROCM_PATH=/opt/rocm-7.14.0 WORK=$PWD/work
./build.sh
python3 run_matrix.py --apps xsbench,rsbench --sizes small,large --verify 0,1 --gpu 0          # XNACK off
python3 run_matrix.py --apps xsbench,rsbench --sizes small,large --verify 0,1 --xnack 1 --gpu 0 \
    --configs pristine,instr-notool,kernel-packet,kernel-host,kernel-host-nomm,full-packet,full-host,full-host-nomm,full-host-nomm-warm,kernel-host-unarmed
python3 run_matrix.py --verify 0 --reps 5 --gpu 0 --out $WORK/results/timing \
    --configs pristine,instr-notool,kernel-packet,kernel-host-nomm,full-host-nomm-warm
GPU=0 ./control_sqwaves.sh
python3 summarize.py status 'x0-*'
```

The scripts are standalone and not wired into the SDK's CMake. XSBench and RSBench are cloned by
`build.sh` at the commits above and patched with `patches/*.patch`. No third-party source is
vendored here.
