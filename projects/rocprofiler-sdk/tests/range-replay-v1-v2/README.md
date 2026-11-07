# Range replay v1 vs v2 on OpenMP offload benchmarks (C and Fortran)

Before/after comparison of range replay **v1** (`733091ace5c`, packet replay only) and **v2** (this
branch at `9ded289d8a8`: packet replay plus host replay, the commit callback, the private replay
queue, copy tickets, and the MPI and concurrency fixes). The workloads are OpenMP offload programs
in C and Fortran, most of them irregular-access kernels.

## Result

- **Correctness is the same or better everywhere.** Every replayed range on v1 and v2 left the
  application with a valid answer: 0 invalid runs out of 560 in the main matrix and 0 out of 60 in
  the memory-pool runs. Every C benchmark's result hash matches the native run bit for bit. v1 and
  v2 packet replay record the same dispatch counts and the same modal counter values on every
  benchmark.
- **v2 replays ranges v1 cannot.** Host replay (v2 only) replays the whole XSBench and RSBench
  simulations and BabelStream's full five-kernel loop (100 dispatches, `dot` reductions included).
  It needs libomptarget's memory pool turned off. v1 packet replay, and v2 packet replay, decline
  those ranges with `MEMORY_COPY_IN_RANGE` and run them once.
- **v2 host replay is the fastest option on kernel ranges.** It is 30 to 40% below packet replay on
  kernel ranges (Jacobi 411 to 262 ms, SpMV 378 to 259 ms, gather 704 to 425 ms, BabelStream triad
  932 to 542 ms). On full ranges that round-trip a large host array it is slower (GUPS 328 to
  368 ms), because the registered host array is restored on every pass.
- **v2 packet replay matches v1 in steady state.** From the second replayed pass on, v2 is within
  1% of v1 (median ratio 1.01). The first replayed pass costs 12 to 14 ms more on v2. That is
  `hsa_queue_create` for the private replay queue, created on first use, once per agent per process.
  v2 needs that queue because replaying on the application's queue can deadlock on ROCr's doorbell
  lock. On these one-range programs v2 packet's range wall time is -1% to +7% of v1's.
- **No new v2 bug was found.** Three suspects were investigated and ruled out; see below.

## Setup

- MI300-class GPUs (gfx942), ROCm 7.14.0 `amdclang` / `amdflang`, `-fopenmp --offload-arch=gfx942`,
  `HSA_XNACK=0`, one process per GPU (`ROCR_VISIBLE_DEVICES`).
- One binary per benchmark serves every configuration. The range replay entry points are resolved
  at run time (`../range-replay-openmp-apps/rr_rt.h`), and the SDK under test is pinned with
  `LD_PRELOAD`. Host mode against v1 exits with "host range replay is not in this SDK", which
  confirms which library the process loaded.
- The tool (`../range-replay-openmp-apps/rr_tool.cpp`) is built against each SDK's own headers
  (`-DRR_TOOL_V1` for v1). It requests 4 passes (the application's own run plus 3 replays) and
  collects `SQ_WAVES` on every pass.
- Configurations: `native` (no SDK), `v1-packet`, `v2-packet`, `v2-host`; plus the controls
  `v2-host-notrack`, `v1-host`, and `*-nomm` (`LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD=0`).
- Main matrix: 5 repetitions of every benchmark, range kind and configuration (560 runs), on 3 GPUs
  in parallel.

### Benchmarks

| name | language | access pattern | per range |
|---|---|---|---|
| spmv | C, Fortran | CSR SpMV, 1M rows x 16 nonzeros, random columns (gather) | 8 iterations x 2 kernels |
| gups | C, Fortran | random XOR updates into a 2^24-entry table (atomics) | 8 kernels |
| hist | C, Fortran | 2^26 skewed keys into 4096 bins (contended atomics) | 8 kernels |
| gather | C, Fortran | `y[perm[i]] = 0.5*y[perm[i]] + x[idx[i]]` over 2^24 (gather/scatter) | 8 kernels |
| chase | C, Fortran | 65,536 walkers x 256 steps of pointer chasing on a 2^24-node cycle | 8 kernels |
| jacobi | Fortran | 4096^2 5-point stencil | 8 iterations x 2 kernels |
| babelstream | Fortran | BabelStream 4.0 OpenMP target: copy, mul, add, triad, dot (`-n 20`) | 100 kernels |
| babel-triad | Fortran | BabelStream `--triad-only` | 20 kernels |
| xsbench | C | XSBench event-based, small (`-m event -s small`) | 1 kernel |
| rsbench | C | RSBench event-based, small | 1 kernel |

Ranges (`RR_RANGE`):

- **kernel**: data is mapped before the range, so the range holds only kernels.
- **full**: the range also re-uploads the input (`target update to`), downloads the result
  (`target update from`), and for XSBench/RSBench includes the `map` clauses and the host reduction.

Every benchmark checks its own answer: the irregular kernels against a host reference, BabelStream
with its `check_solution`, and XSBench/RSBench with their verification checksums.

## Results

Median range wall time over 5 runs, measured by the application across all 4 passes. "copy-decl" and
"alloc-decl" mean the range was declined (`MEMORY_COPY_IN_RANGE` / `ALLOCATION_CHANGED_IN_RANGE`)
and ran once; the answer is still correct. The full table, with statuses, dispatch counts, per-pass
times and counter agreement, is in [`results/summary.md`](results/summary.md).

| bench | range | native | v1 packet | v2 packet | v2 host | v2 host, pool off |
|---|---|---|---|---|---|---|
| c-chase | full | 3.5 | 201.4 | 212.4 | 159.9 | - |
| c-chase | kernel | 3.5 | 200.5 | 208.7 | 160.8 | - |
| c-gather | full | 15.3 | 687.8 | 705.2 | 554.5 | - |
| c-gather | kernel | 9.9 | 675.9 | 704.2 | 425.4 | - |
| c-gups | full | 22.7 | 317.8 | 327.5 | 367.5 | - |
| c-gups | kernel | 17.3 | 318.0 | 324.6 | 246.4 | - |
| c-hist | full | 292.6 | 1534.3 | 1543.7 | 1402.5 | - |
| c-hist | kernel | 292.0 | 1536.2 | 1546.2 | 1406.5 | - |
| c-rsbench | full | 108.0 | 202.0 copy-decl | 202.0 copy-decl | 200.0 alloc-decl | 420.0 |
| c-rsbench | kernel | 97.0 | 440.0 | 455.0 | 242.0 alloc-decl | 414.0 |
| c-spmv | full | 9.0 | 364.1 | 379.1 | 258.6 | - |
| c-spmv | kernel | 8.6 | 378.5 | 377.8 | 258.5 | - |
| c-xsbench | full | 100.0 | 204.0 copy-decl | 204.0 copy-decl | 205.0 alloc-decl | 407.0 |
| c-xsbench | kernel | 99.0 | 636.0 | 653.0 | 376.0 alloc-decl | 471.0 |
| f-babel-triad | kernel | 4.8 | 940.5 | 932.3 | 542.3 | - |
| f-babelstream | kernel | 63.8 | 545.4 copy-decl | 544.1 copy-decl | 558.4 alloc-decl | 690.5 |
| f-chase | full | 3.8 | 197.7 | 212.2 | 156.8 | - |
| f-chase | kernel | 3.5 | 203.6 | 213.2 | 162.8 | - |
| f-gather | full | 15.8 | 685.0 | 708.2 | 558.5 | - |
| f-gather | kernel | 10.1 | 690.4 | 697.2 | 426.0 | - |
| f-gups | full | 22.8 | 319.9 | 333.8 | 366.8 | - |
| f-gups | kernel | 17.6 | 314.9 | 332.1 | 243.6 | - |
| f-hist | full | 291.8 | 1536.7 | 1550.0 | 1402.1 | - |
| f-hist | kernel | 292.0 | 1536.8 | 1547.8 | 1398.1 | - |
| f-jacobi | full | 7.3 | 397.0 | 416.3 | 380.7 | - |
| f-jacobi | kernel | 1.9 | 402.0 | 411.3 | 262.4 | - |
| f-spmv | full | 9.0 | 377.2 | 396.0 | 267.2 | - |
| f-spmv | kernel | 8.4 | 378.1 | 389.2 | 262.6 | - |

Most of each replayed range's time is fixed cost of the application's own pass, which is the same on
v1 and v2: the entry snapshot of every device allocation (77 MB in 30 regions for chase, for
example), counter setup, and the exit snapshot. The comparison between versions is in the replayed
passes:

| bench | range | v1 packet pass 1 / pass 2 ms | v2 packet pass 1 / pass 2 ms | v2 host pass 1 / pass 2 ms |
|---|---|---|---|---|
| c-chase | kernel | 6.2 / 5.9 | 19.0 / 6.2 | 6.2 / 6.1 |
| c-gather | kernel | 23.0 / 22.8 | 36.3 / 23.1 | 23.0 / 23.0 |
| c-gups | kernel | 21.4 / 21.1 | 35.6 / 21.4 | 21.3 / 22.2 |
| c-gups | full | 21.4 / 21.1 | 33.4 / 21.3 | 37.4 / 37.1 |
| c-hist | kernel | 297.0 / 296.7 | 308.5 / 296.2 | 300.3 / 295.6 |
| c-spmv | kernel | 15.9 / 15.3 | 28.8 / 15.6 | 15.5 / 15.5 |
| c-xsbench | kernel | 25.6 / 25.5 | 38.7 / 26.0 | - |
| f-babel-triad | kernel | 25.1 / 24.5 | 31.3 / 24.6 | 24.5 / 24.5 |
| f-jacobi | kernel | 11.6 / 11.1 | 24.9 / 11.1 | 11.1 / 11.2 |
| f-jacobi | full | 11.5 / 10.9 | 23.9 / 9.4 | 25.8 / 25.6 |

The SDK's INFO log (`ROCPROFILER_LOG_LEVEL=info`) confirms where the first-pass cost goes. For
chase, v1 reports `3 passes 11.3 ms, 3 restores 6.7 ms` and v2 reports `3 passes 24.8 ms,
3 restores 6.8 ms`, with the same 77 MB snapshot; the tool's per-pass times are 19.5 / 6.2 / 10.3 ms
on v2 against 6.2 / 5.9 / 10.1 ms on v1 (see [`results/phase-timings.txt`](results/phase-timings.txt);
v2's third run was slower in every phase, including the application's own pass). The
private queue is made by `private_queue()` in `hsa/replay_window.cpp` on the first replayed
submission. Creating it while the range is recorded, off the critical path, would remove the
difference; that is an optimization, not a correctness issue, and is not part of this change.

Host replay re-runs the application's own code, including its `target update` copies, so a full
range's passes also pay for the copies and for restoring the registered host array (128 MB for
GUPS). That is why host replay wins on kernel ranges and loses on GUPS/gather/Jacobi full ranges.

### What only v2 can replay

| bench | range | v1 packet | v2 packet | v2 host (pool on) | v2 host, `LIBOMPTARGET_MEMORY_MANAGER_THRESHOLD=0` |
|---|---|---|---|---|---|
| xsbench | full | `MEMORY_COPY_IN_RANGE` | `MEMORY_COPY_IN_RANGE` | `ALLOCATION_CHANGED_IN_RANGE` | **REPLAYED**, checksum valid 3/3 |
| xsbench | kernel | REPLAYED | REPLAYED | `ALLOCATION_CHANGED_IN_RANGE` | **REPLAYED** |
| rsbench | full | `MEMORY_COPY_IN_RANGE` | `MEMORY_COPY_IN_RANGE` | `ALLOCATION_CHANGED_IN_RANGE` | **REPLAYED**, checksum valid 3/3 |
| rsbench | kernel | REPLAYED | REPLAYED | `ALLOCATION_CHANGED_IN_RANGE` | **REPLAYED** |
| babelstream (all 5 kernels) | kernel | `MEMORY_COPY_IN_RANGE` | `MEMORY_COPY_IN_RANGE` | `ALLOCATION_CHANGED_IN_RANGE` | **REPLAYED**, 100 dispatches, `SQ_WAVES` equal on all passes |

libomptarget's memory pool keeps a block that was freed inside the range, so the range ends with an
allocation it did not start with, and host replay declines rather than restore into it. With the
pool off the allocation is balanced and the range replays. In packet mode, with the pool off, v1
and v2 both decline the XSBench/RSBench kernel ranges with `ALLOCATION_CHANGED_IN_RANGE`; with the
pool on they replay them.

## Suspects investigated (no v2 bug)

1. **Host replay gave wrong answers on full ranges.** The full range writes host memory
   (`target update from`) and then reads it back (`target update to`) on the next pass. That host
   state must be registered with `rocprofiler_range_replay_host_track_memory`, as
   `range_replay_host.md` requires; the benchmarks now register it (`rr_track()`). The negative
   control `v2-host-notrack` (`RR_NO_TRACK=1`) skips the registration: every irregular full range
   then reports `REPLAYED` and fails its own answer check (22/22 runs), as documented. With
   `ROCPROF_RANGE_REPLAY_VERIFY=1`, v2's divergence check flags all of them (`divergence_count` 1 to
   2) and reports 0 when the memory is registered.
2. **The irregular full ranges contain a host-to-device copy, but packet mode replays them.** The
   `target update to` comes after `begin` and before the range's first kernel. A packet range binds
   to its agent at its first dispatch, so the copy is not seen as in-range. That is sound on v2: the
   copy holds a ticket on its agent until the copy engine finishes it, and the entry snapshot's
   agent drain (`replay_drain_agent_or_fatal`) waits for those tickets. The copy's result is
   therefore in the snapshot, and every replayed pass starts from it. (v1 has no copy tickets; it
   is correct here only because libomptarget's `target update` is synchronous.)
3. **`SQ_WAVES` sometimes differs between passes.** In the parallel matrix, 54 of 365 replayed
   ranges (15%; v1 packet 18/125, v2 packet 22/125, v2 host 14/115) had one pass whose `SQ_WAVES` was higher than the others, often by exactly one dispatch's
   wave count (4864 to 9728 for GUPS). This happened at the same rate on v1 and v2 and occasionally
   in pass 0, the application's own run. Stock `rocprofv3 --pmc SQ_WAVES` (ROCm 7.14, no range
   replay) shows the same over-count when three processes collect counters on three GPUs at once
   (1 of 288 dispatches read 12160 instead of 4864), and none when one process runs alone (0 of 96).
   It is a counter-collection effect of concurrent profiling processes, not a range replay effect.
   Run serially on one GPU, see [Serial counter check](#serial-counter-check).

## Serial counter check

The same benchmarks, ranges and configurations (`v1-packet`, `v2-packet`, `v2-host`), 2 repetitions,
one process at a time on one GPU ([`results/serial-summary.md`](results/serial-summary.md)):

| | v1 packet | v2 packet | v2 host |
|---|---|---|---|
| replayed ranges with `SQ_WAVES` unequal across passes | 0 / 50 | 0 / 50 | 0 / 46 |
| runs with an invalid answer | 0 | 0 | 0 |

168 runs, 146 replayed ranges, every pass of every range read the same `SQ_WAVES`. The per-record
log (`RR_RECORD_LOG=1`) shows the same split for v2: 1,024 of 1,024 records exact serially, and 7
of 1,152 over-counted with three processes at once
([`results/stock-rocprofv3-concurrency.txt`](results/stock-rocprofv3-concurrency.txt)).

## Reproducing

```bash
SDK_V1_BUILD=<v1 build> SDK_V1_SRC=<v1 projects/rocprofiler-sdk> SDK_V2_BUILD=<this build> \
  ROCM_PATH=/opt/rocm WORK=/tmp/rr-v1v2 ./build.sh
RR_LIBS=/tmp/rr-v1v2/libs RR_BIN=/tmp/rr-v1v2/bin python3 compare_v1v2.py --reps 5 --gpus 5,6,7
RR_LIBS=/tmp/rr-v1v2/libs RR_BIN=/tmp/rr-v1v2/bin python3 compare_v1v2.py --reps 3 \
  --benches c-xsbench,c-rsbench,f-babelstream \
  --configs native-nomm,v1-packet-nomm,v2-packet-nomm,v2-host-nomm --gpus 5,6
python3 summarize_v1v2.py results-v1v2/results.jsonl
```

`compare_v1v2.py --configs v2-host-notrack --ranges full` runs the negative control, and
`RR_RECORD_LOG=1` makes the tool print every counter record (pass, dispatch id, kernel, value).

## Files

- `build.sh`: builds both SDK tool variants and every benchmark into `$WORK`.
- `compare_v1v2.py`: runs the matrix and writes one JSON row per run.
- `summarize_v1v2.py`: turns the rows into the tables above.
- `irregular/`: the irregular-access benchmarks (`irregular.c`, `irregular.f90`) and the Fortran
  range bridge (`rr_f.c`, which keeps the `sigsetjmp` frame live around a Fortran body).
- `babelstream/babelstream-fortran-rr.patch`: wraps BabelStream's timed loop (commit `5cb9445`) in
  range 1.
- `results/`: `results.jsonl` (main matrix plus controls), `summary.md`, `summary.json`,
  `serial-results.jsonl`, `serial-summary.md`, `phase-timings.txt`,
  `stock-rocprofv3-concurrency.txt`.
