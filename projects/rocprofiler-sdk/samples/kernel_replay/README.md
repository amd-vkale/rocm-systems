# Kernel replay samples

Hands-on companions to the kernel replay guide
([`using-kernel-replay.rst`](../../source/docs/how-to/using-kernel-replay.rst)). Each sample is a
small **custom tool** (`lib<name>-client.so`) preloaded onto one shared HIP application
(`main.cpp`), and each tool wires kernel replay plus at most one service, so you can read the whole
thing in a few minutes.

These are SDK samples for tool authors. To collect several counter groups from the command line
instead, use `rocprofv3 --replay-mode kernel --kernel-replay-beta-enabled`
([`using-kernel-replay-rocprofv3.rst`](../../source/docs/how-to/using-kernel-replay-rocprofv3.rst)).

## Build and run

Build the samples with `-DROCPROFILER_BUILD_SAMPLES=ON` (or from an installed ROCm, as described in
the samples how-to), then from the samples build directory:

```bash
ctest -R '^kernel-replay-' --output-on-failure
```

Or run one by hand:

```bash
export LD_PRELOAD=./libkernel-replay-counters-client.so
./kernel-replay-counters
```

Each tool checks its own expectations in `tool_fini` and aborts on a mismatch, so a passing run is
the demonstration.

## Suggested reading order

| # | Sample | Source | Passes | What to look for |
|---|---|---|---|---|
| 1 | `kernel-replay-basic` | `basic_client.cpp` | 4 | The minimum: subscribe, set `replay_pass_count` in CONFIG, count PASS callbacks. The app still sees one kernel completion. |
| 2 | `kernel-replay-counters` | `counters_client.cpp` | 3 | A different counter group per pass: PASS publishes `current_pass` in a `thread_local`, the dispatch-counting callback picks the config for it. |
| 3 | `kernel-replay-opt-out` | `opt_out_client.cpp` | 3 / 1 | Per-dispatch policy: replays the `bump` kernel (`workgroup_size.x == 67`), leaves `nudge` alone by keeping `replay_pass_count` NULL. |
| 4 | `kernel-replay-early-exit` | `early_exit_client.cpp` | 4 / 2 | `replay_continue` stops after pass 1 even though `replay_pass_count` asked for 4. |
| 5 | `kernel-replay-basic-user-data` | `basic_client_with_user_data.cpp` | 4 max / 2 actual | `user_data` semantics: state behind `user_data.ptr`, and a PASS-exit decision handed to `replay_continue` through the pass-scoped copy. |
| 6 | `kernel-replay-att` | `att_client.cpp` | 2 | Different services on different passes: counters on pass 0, thread trace on pass 1, using the local start/stop toggles. |
| 7 | `kernel-replay-spm` | `spm_client.cpp` | 2 | The same, with SPM on pass 1. |

`kernel-replay-counters-then-pc-sampling` (`counters_then_pc_sampling_client.cpp`) is built but
**disabled in CTest**: PC sampling is agent-wide and ignores the local toggles, so it cannot be
isolated to one pass.

## What you can change in these samples

| Dial | Where to look |
|---|---|
| Pass count | `kPasses` / `replay_pass_count` in any client |
| Which dispatches replay | the `workgroup_size.x` check in `opt_out_client.cpp` |
| Early stop | `replay_continue` in `early_exit_client.cpp` |
| Per-dispatch state | `user_data` handling in `basic_client_with_user_data.cpp` |
| Which service runs on which pass | the PASS-enter toggles in `att_client.cpp`, `spm_client.cpp` |
| Counter group per pass | `counter_name_for_pass` in `counters_client.cpp` |

The header with every dial is
[`experimental/kernel_replay.h`](../../source/include/rocprofiler-sdk/experimental/kernel_replay.h).

## Advanced: multi-service pass ordering

`kernel-replay-services-first` and `kernel-replay-services-last` share one larger client
(`service_sequence_client.cpp`) that runs **five** passes with ATT, SPM, and counters in a fixed
order. PC sampling slots are present in the source, but these CTest targets are **disabled** until
PC sampling honors localized overrides.

| Sample | Pass order (5 passes) |
|---|---|
| `kernel-replay-services-first` | *(disabled)* PC sampling → ATT → SPM → counters → counters |
| `kernel-replay-services-last` | *(disabled)* Counters → counters → PC sampling → ATT → SPM |

Set `KR_SERVICE_ORDER=services-first` or `services-last` (the CTest targets set this for you).
Requires `ROCPROFILER_PC_SAMPLING_BETA_ENABLED=ON` and `ROCPROFILER_SPM_BETA_ENABLED=True`.

## Incompatible services use separate passes

- Dispatch counter collection turns clock gating back on around a kernel, while PC sampling on
  MI2xx/MI3xx requires it off. Running both on the **same** pass can hang the GPU.
- ATT and SPM both inject AQL instrumentation around the dispatch, so they cannot safely share a
  pass.

Put each on its own context and its own pass, and use the local toggles to switch between them.
`ROCPROFILER_SPM_BETA_ENABLED=True` is required for the SPM sample, and
`ROCPROFILER_PC_SAMPLING_BETA_ENABLED=ON` for the PC-sampling sample.

## Samples vs integration tests

| | **Samples** (`samples/kernel_replay/`) | **Integration tests** (`tests/kernel-replay-*`) |
|---|---|---|
| Purpose | Teach: subscribe to replay, wire one service | Regression coverage (local toggles, concurrency) |
| Tool | Small preloaded client with only the feature under demo | Env-driven client in `tests/` |
| Run | `ctest -R '^kernel-replay-'` in the samples build | `ctest -R kernel-replay-local-context` in the main build |

`rocprofv3`'s use of kernel replay (JSON `replay_pass`, CLI validation) is tested under
`tests/rocprofv3/kernel-replay/`, not here.

## See also

- [`using-kernel-replay.rst`](../../source/docs/how-to/using-kernel-replay.rst) — SDK how-to
- [`conceptual/kernel_replay/index.md`](../../source/docs/conceptual/kernel_replay/index.md) — how it works
- [`samples/range_replay/`](../range_replay/README.md) — replaying a sequence of dependent kernels
