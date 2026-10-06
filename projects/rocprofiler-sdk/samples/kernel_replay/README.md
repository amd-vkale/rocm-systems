# Kernel replay samples

Small **custom tools** plus a HIP application, following the same layout as
`samples/counter_collection/`: each sample is a shared-library client (`.so`) preloaded
onto a minimal app (`main.cpp`). These are **not** `rocprofv3` integration tests.

Build with `-DROCPROFILER_BUILD_SAMPLES=ON`.

## Samples vs integration tests

| | **Samples** (`samples/kernel_replay/`) | **Integration tests** (`tests/kernel-replay-*`) |
|---|---|---|
| Purpose | Teach tool authors: subscribe to replay, wire one service | Regression coverage (concurrency, overhead) |
| Tool | Small LD_PRELOAD client with only the feature under demo | Env-driven client in `tests/` |
| App | Shared `main.cpp` HIP kernels | Dedicated test app per suite |
| Run | `ctest -R '^kernel-replay-'` in the samples build dir | `ctest -R kernel-replay-concurrency` in main build |

For JSON output validation with the shared test harness, see `tests/counter-collection/`
(`rocprofiler-sdk-json-tool`). Kernel replay JSON/tool wiring belongs to `rocprofv3`, not to
these SDK samples.

## What each sample shows

Start with the **basic** samples — each client is small and only wires kernel replay plus one
service (same idea as `samples/counter_collection/`).

| Sample | Passes | Services |
|---|---|---|
| `kernel-replay-basic` | 4 | Replay only. The app still sees one kernel completion. |
| `kernel-replay-basic-user-data` | 4 max / 2 actual | Carries tool state through `user_data.ptr`; PASS state stops replay through `replay_continue`. |
| `kernel-replay-counters` | 3 | Dispatch counters; each pass selects a different counter configuration. |
| `kernel-replay-att` | 2 | ATT on every pass. |
| `kernel-replay-spm` | 2 | SPM on every pass. |
| `kernel-replay-opt-out` | 3 / 1 | Replays the `bump` kernel (`block.x == 67`); leaves `nudge` unreplayed. |
| `kernel-replay-early-exit` | 4 / 2 | Sets `replay_continue` to stop after pass 1 even though `replay_pass_count` returns 4. |

## Incompatible services must not share a replay

Every pass of a replay runs every context that is active, so services that cannot share a
dispatch must not be active in the same replay:

- Dispatch counter collection turns clock gating back on around a kernel, while PC sampling on
  MI2xx/MI3xx requires clock gating off. Running both on the same dispatch can hang the GPU.
- ATT and SPM both inject AQL instrumentation around the dispatch, so they cannot share one either.

Collect such services in separate runs. Within one service, a tool can still vary the work per
pass, for example the counter configuration it returns for each pass (`kernel-replay-counters`).

`ROCPROFILER_SPM_BETA_ENABLED=True` is required for the SPM sample.

## Run

From the sample build directory:

```bash
ctest -R '^kernel-replay-' --output-on-failure
```

Or manually:

```bash
export LD_PRELOAD=./libkernel-replay-counters-client.so
./kernel-replay-counters
```
