(kernel-replay-conceptual)=
# Kernel Replay (Experimental)

**Kernel replay runs one GPU dispatch several times inside a single application run**, restoring
device memory between runs so every pass sees identical inputs. The application observes one kernel
completion and the memory its kernel produced; the extra passes are invisible to it.

The classic use is hardware counters. Each counter block has a limited number of registers, so a
large set of counters needs several passes. *Application replay* re-runs the whole program once per
group; kernel replay does it per dispatch instead, in one run:

| Approach | What repeats | Memory handling | Cost |
|---|---|---|---|
| Application replay | the whole application, once per group | none; each run is a fresh process | `N ×` app runtime |
| **Kernel replay** | one dispatch, in place | device memory snapshot and restore between passes | `N ×` kernel time `+` snapshot/restore per dispatch |
| [Range replay](../range_replay/index.md) | a phase the application marks | the same snapshot, once per range | `N ×` phase time `+` snapshot/restore per range |
| Counter group rotation | nothing; successive dispatches sample different groups | none | `1 ×` app runtime, not the same dispatch |

## How it works, in one paragraph

When a tool subscribes to the `ROCPROFILER_CALLBACK_TRACING_KERNEL_REPLAY` callback domain, the SDK
intercepts each dispatch and asks the tool (`CONFIG`) how many passes it wants. If more than one, it
drains the GPU, snapshots tracked device memory, and runs the dispatch that many times on the
submitting thread, restoring the snapshot before each pass after the first and raising a `PASS`
callback around each. Then it releases the application's completion once. There is no replay
thread and no change to the application.

Because replay is a callback domain rather than a counter mode, it is not tied to counters. A tool
decides what each pass is for and, with **localized context toggles**, which of its services are
active on which pass -- counters on one pass, thread trace or SPM on another.

## Ways to use it

- **rocprofv3:** `--replay-mode kernel --kernel-replay-beta-enabled` collects every `--pmc` group in
  one run. Start with {ref}`using-kernel-replay-rocprofv3`.
- **Custom tools:** choose the pass count per dispatch, stop early or loop adaptively, carry state
  through `user_data`, and place services on specific passes. Start with {ref}`using-kernel-replay`.
- **Phases of dependent kernels:** use [range replay](../range_replay/index.md).

Kernel replay is **experimental**: the header is under `rocprofiler-sdk/experimental/` and the API is
expected to change. Snapshot and restore are a full in-memory copy (no dirty-page hashing yet).
Several waits inside the replay window abort the process on expiry rather than continue on
questionable state, a deliberate beta choice described in
[Concurrency and isolation](kernel_replay_concurrency_and_isolation.md).

## Where to go next

**rocprofv3 users:** {ref}`using-kernel-replay-rocprofv3`.

**Tool authors:** {ref}`using-kernel-replay`, then {ref}`kernel-replay-sdk-api`.

**Deeper reading:**

- **[Callback API and tool configuration](kernel_replay_callback_api.md)** — the domain's two
  operations, the payload, pass-count semantics, dispatch identity, and localized context control.
- **[Memory snapshot and restore](kernel_replay_memory_snapshot.md)** — what is captured and
  excluded, module-scope variables, and the decline-rather-than-corrupt policy.
- **[Concurrency and isolation](kernel_replay_concurrency_and_isolation.md)** — the per-agent lock,
  drains, bounded waits, and what is deliberately not isolated.
- **[Performance assessment](kernel_replay_performance.md)** — the per-dispatch cost model and how it
  scales across accelerator generations.
- **[Test coverage](kernel_replay_testing.md)** — what is tested at each level and the known gaps.
- **[Downstream tools](kernel_replay_downstream_tools.md)** — what rocprofiler-compute,
  rocprofiler-systems, and the rocprofv3 lite-trace path would need to use replay.
- **[Callback tracing API design](kernel_replay_callback_api_design.md)** — design rationale and
  history; read the pages above for current behavior.
- **Samples:** `samples/kernel_replay/` — small tools for counters, opt-out, early exit,
  `user_data`, ATT, and SPM.

## Source map

Paths relative to `projects/rocprofiler-sdk/source/`:

| What | Where |
|---|---|
| Public API and every tool dial | `include/rocprofiler-sdk/experimental/kernel_replay.h` |
| CONFIG / PASS callbacks, pass-count and continue decisions | `lib/rocprofiler-sdk/kernel_replay/replay_callbacks.cpp` |
| Localized context toggles | `lib/rocprofiler-sdk/kernel_replay/local_context.cpp` |
| Snapshot and restore | `lib/rocprofiler-sdk/kernel_replay/memory_snapshot.cpp` |
| The replay window (lock, drains, pass loop) | `lib/rocprofiler-sdk/hsa/queue.cpp` |
| rocprofv3's kernel replay policy | `lib/rocprofiler-sdk-tool/tool.cpp` (`kernel_replay_callback`) |
