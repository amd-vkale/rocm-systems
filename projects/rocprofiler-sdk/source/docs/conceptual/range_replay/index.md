(range-replay-conceptual)=
# Range Replay (Experimental)

**Range replay measures a whole phase of an application several times in one run.** The
application marks a phase -- a training step, a transformer layer, a solver iteration -- and the SDK
re-executes it once per extra pass, starting each time from the device-memory state the phase began
with. A tool can collect a different counter group, or run a different service, on every pass and
compare the results directly.

It exists because [kernel replay](../kernel_replay/index.md), which repeats **one dispatch**, does
not fit a phase whose kernels feed each other: re-running one kernel in isolation measures inputs
the rest of the phase has already changed.

| | Kernel replay | Range replay |
|---|---|---|
| Unit of repetition | one dispatch | a sequence of dispatches the application marks |
| Who runs pass 0 | the SDK | the application itself (the SDK observes) |
| When extra passes run | inside the dispatch, before the application sees it complete | when the application calls `end` |
| Code changes | none | the application brackets the phase |
| Snapshot cost | once per replayed dispatch | once per range |

## Record live, then replay

```text
rocprofiler_range_replay_begin(id)   tool answers CONFIG with a pass count
  ... the application's kernels ...  recorded; device memory snapshotted before the first one.
                                     This is pass 0: the application's own execution.
rocprofiler_range_replay_end()       for pass 1 .. N-1:
                                       restore the entry snapshot, PASS enter,
                                       re-execute the range, PASS exit, continue?
                                     CLOSE: replayed, or why not
```

There are two ways to re-execute a range:

- **Packet replay** (default) re-submits the recorded kernel packets. It is cheap, and requires the
  range to be kernels on one stream and one GPU, with no copies, graphs, or allocations inside.
- **Host replay** re-runs the application's own code between the markers (`sigsetjmp` /
  `siglongjmp`). It handles copies, memsets, several streams and GPUs, graphs, balanced allocations,
  and host computation. See [Host range replay](range_replay_host.md).

**Soundness is checked, not enforced.** The SDK cannot force a range to be self-contained, so it
checks every condition it can observe and **declines** a range that fails one. A decline is not an
error: the application's own run is untouched, the extra passes are skipped, and the tool learns why
from `CLOSE`. See [Soundness and declining](range_replay_soundness.md).

## Ways to use it

- **rocprofv3:** `--replay-mode range --range-replay-beta-enabled` collects one `--pmc` group per
  pass of every range the application marks; `--range-replay-host` selects host replay. Start
  with {ref}`using-range-replay-rocprofv3`.
- **Custom tools:** subscribe to `ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY` and choose the pass
  count, early stop, host replay, cross-process commit, and per-pass service toggles. Start with
  {ref}`using-range-replay`.

Range replay shares kernel replay's device-memory snapshot, per-agent replay window, pass-count
semantics, and localized context toggles, so a tool that already drives kernel replay reuses most of
its pass logic. The header is `rocprofiler-sdk/experimental/range_replay.h`; the API is expected to
change before a stable release.

## What is not supported

Rows marked *(packet only)* are lifted by host replay.

| Not supported | Consequence |
|---|---|
| More than one queue or GPU per range *(packet only)* | declined (`MULTI_QUEUE` / `MULTI_AGENT`) |
| Copies into device memory, HIP graphs, host state that feeds kernels *(packet only)* | declined, or (host state) not rewound |
| Unified / managed, host, fine-grained, VM-mapped memory | not rewound; use `ROCPROF_RANGE_REPLAY_VERIFY=1` to detect |
| RCCL collectives inside a range | declined (`COLLECTIVE_IN_RANGE`) |
| Code objects loaded or unloaded inside a range | declined (`CODE_OBJECT_CHANGED_IN_RANGE`); warm up first |
| Device writes to another process's memory (rocSHMEM, IPC) | repeated on every pass; warned |
| Kernels that `printf` | output repeated (hostcall) or lost (buffered) on extra passes |
| Concurrency within a pass *(packet only)* | kernels serialized; concurrency-sensitive counters differ from pass 0 |

## Where to go next

**rocprofv3 users:** {ref}`using-range-replay-rocprofv3`.

**Tool and application authors:** {ref}`using-range-replay`, then {ref}`range-replay-sdk-api`.

**Deeper reading:**

- **[Callback API and tool configuration](range_replay_callback_api.md)** — the domain's three
  operations, the payload, pass-count semantics, committing across processes, and local toggles.
- **[Soundness and declining](range_replay_soundness.md)** — what makes a range replayable, the full
  decline table, cross-thread declines, the replay queue, and the divergence check.
- **[Host range replay](range_replay_host.md)** — the `sigsetjmp` / `siglongjmp` path, registered
  host memory, and the application's contract.
- **[Multi-GPU and collectives](range_replay_multi_gpu.md)** — MPI ranks, agreeing with
  `replay_commit_cb`, what to bracket, and measured results on eight GPUs.
- **[Performance](range_replay_performance.md)** — the cost model and why a declined range must never
  be timed.
- **Samples:** `samples/range_replay/` — a three-dispatch application with basic, host, opt-out, and
  decline tools.

## Source map

Paths relative to `projects/rocprofiler-sdk/source/`:

| What | Where |
|---|---|
| Public API and every tool dial | `include/rocprofiler-sdk/experimental/range_replay.h` |
| begin / end | `lib/rocprofiler-sdk/range_replay/api.cpp` |
| Eligibility (decline) decisions | `lib/rocprofiler-sdk/range_replay/range_state.cpp` |
| Packet replay loop | `lib/rocprofiler-sdk/range_replay/executor.cpp` |
| Host replay | `lib/rocprofiler-sdk/range_replay/host_replay.cpp` |
| rocprofv3's range replay policy | `lib/rocprofiler-sdk-tool/tool.cpp` (`range_replay_callback`) |
