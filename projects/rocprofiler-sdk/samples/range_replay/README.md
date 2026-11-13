# Range replay samples

Hands-on companions to the range replay guide
([`using-range-replay.rst`](../../source/docs/how-to/using-range-replay.rst)). Each sample is a
small **custom tool** (`lib<name>-client.so`) preloaded onto one shared HIP application
(`main.cpp`) that marks a three-kernel range. Read `main.cpp` for the application side and any
client for the tool side; each fits on a screen or two.

These are SDK samples. To collect several counter groups over a marked range from the command line,
use `rocprofv3 --replay-mode range --range-replay-beta-enabled`
([`using-range-replay-rocprofv3.rst`](../../source/docs/how-to/using-range-replay-rocprofv3.rst)).

## Build and run

Build the samples with `-DROCPROFILER_BUILD_SAMPLES=ON` (or from an installed ROCm, as described in
the samples how-to), then from the samples build directory:

```bash
ctest -R '^range-replay-' --output-on-failure
```

Or run one by hand (the CTest targets also set the environment shown in the table below):

```bash
export LD_PRELOAD=./librange-replay-basic-client.so
ROCPROF_RANGE_REPLAY_VERIFY=1 ./range-replay-basic
```

Each tool checks its expectations in `tool_fini` and aborts on a mismatch, so a passing run is the
demonstration.

## Suggested reading order

| # | Sample | Source | Environment (set by CTest) | CLOSE status | What to look for |
|---|---|---|---|---|---|
| 1 | `range-replay-basic` | `basic_client.cpp` | `ROCPROF_RANGE_REPLAY_VERIFY=1` | `REPLAYED` | The whole loop: one CONFIG, PASS callbacks for passes 1-3, one CLOSE, and a zero divergence count. |
| 2 | `range-replay-opt-out` | `opt_out_client.cpp` | — | `NO_PASS_COUNT` | Leaving `pass_count_cb` NULL is the per-range opt-out: the range is opened, tracked, and closed, but no pass runs. |
| 3 | `range-replay-decline` | `decline_client.cpp` | `RR_APP_MODE=multi-queue`, `GPU_MAX_HW_QUEUES=4` | `MULTI_QUEUE` | A range the SDK refuses. CLOSE names the reason, and the application's result is untouched. |
| 4 | `range-replay-host` | `basic_client.cpp` | `RR_APP_MODE=host`, `RR_HOST_REPLAY=1`, `ROCPROF_RANGE_REPLAY_VERIFY=1` | `REPLAYED` | The same checks as `basic`, but passes 1-3 re-enter `main` through `siglongjmp` instead of re-submitting recorded packets. |

## What you can change in these samples

| Dial | Where to look |
|---|---|
| Pass count | `kPasses` and `pass_count` in `basic_client.cpp` (4 = live run + 3 re-executions) |
| Packet vs host replay | `data->host_replay` in `basic_client.cpp` (driven by `RR_HOST_REPLAY`) and the `sigsetjmp` branch in `main.cpp` (driven by `RR_APP_MODE=host`) |
| Per-range opt-out | `opt_out_client.cpp` (no `pass_count_cb`) |
| What the range contains | `RR_APP_MODE` in `main.cpp`: `plain`, `multi-queue`, or `host` |
| Divergence check | `ROCPROF_RANGE_REPLAY_VERIFY=1` |

Further dials (`replay_continue_cb`, `replay_commit_cb`, local context toggles, `user_data`) are
covered in the guide's "What you can configure" table and exercised by
`tests/range-replay-host/` and `tests/range-replay-local-context/`. The header with every dial is
[`experimental/range_replay.h`](../../source/include/rocprofiler-sdk/experimental/range_replay.h).

## How range replay differs from kernel replay

Kernel replay re-executes one dispatch **in place**, before the application observes its
completion, so the application never knows it happened and the tool alone drives it.

A range covers a *sequence* of dispatches the application has **already observed**. There is no
"in place" left, so the SDK records the range while it runs and re-executes the recording
afterwards:

- **pass 0** is the application's own execution, which the SDK observes rather than drives, and
  which raises no PASS callback
- **passes 1..N-1** are SDK-driven re-executions from the memory state captured at range entry

That is also why the application is a participant here and not just a subject: only it knows where
its range boundaries are. `main.cpp` calls `rocprofiler_range_replay_begin` / `_end` around the
dispatches it wants replayed. It resolves those entry points with `dlsym` out of the preloaded
client rather than linking the SDK, because linking the SDK into the HIP executable makes HIP
report `hipErrorInvalidDeviceFunction` on the first kernel launch on gfx942. The same pattern lets
an application run normally when no profiler is loaded.

## The application

`main.cpp` runs three dispatches of one kernel inside a range:

```
acc = 0  ->  acc*3+1  ->  acc*3+2  ->  acc*3+3   ==  18
```

Each dispatch reads what its predecessor wrote, so the chain is order-dependent and not
idempotent. Two separate checks use it, and they verify different things:

- **`acc == 18` in the application** says the replay window was **transparent**: the application
  saw exactly the value its own execution produced. The executor restores the range-exit state
  after the last pass, so this covers that restore and the window not corrupting device memory
  along the way. All samples check it, including the two where no pass runs.
- **`divergence_count == 0` at `CLOSE`** says the range was **repeatable**: the state after the
  final replayed pass matched the state the application's own execution left behind. This is the
  check that a between-pass rewind actually happened and that the recording replayed in order —
  the range-exit restore runs last and would otherwise hide both. It needs
  `ROCPROF_RANGE_REPLAY_VERIFY=1`, which the `range-replay-basic` and `range-replay-host` CTest
  targets set.

The three dispatches share a kernel object but differ in their kernargs, so the replay has to
stage a distinct kernarg slot for each of them.

`RR_APP_MODE` selects what else the application does inside the range:

| Mode | Inside the range |
|---|---|
| `plain` (default) | Three dispatches on one stream |
| `multi-queue` | The same, plus a dispatch on a second stream |
| `host` | The same as `plain`, opened with `sigsetjmp` so a tool can `siglongjmp` back to the top of the range |

`multi-queue` also needs `GPU_MAX_HW_QUEUES` to be at least 2 (the CTest target pins it to 4).
HIP pools hardware queues and round-robins streams onto them, so with a pool of one the two
streams would share a queue and the range would be replayed rather than declined.

## Declines

`range-replay-decline` is the shape every decline takes, not a special case. A packet-replay range
is replayed only when every recorded dispatch targets one queue on one agent, no HIP graph launch
occurs inside it, no async copy writes device memory inside it, no device allocation is made or
freed inside it, and no other thread dispatches to the same agent while it is open. Each of those
has its own `rocprofiler_range_replay_status_t`, and all of them reach the tool the same way: the
application's live run completes normally, and CLOSE reports why the extra passes were skipped.
The sample triggers `MULTI_QUEUE` because two HIP streams are the one decline an application can
provoke deterministically on a single GPU. The guide has a table of every status and its fix.

## Divergence checking

Set `ROCPROF_RANGE_REPLAY_VERIFY=1` to have the SDK hash the snapshot regions after the final pass
and compare them against the state the application's own execution produced. CLOSE then reports
`divergence_count`: the number of regions that differed. A non-zero count means the range is not
self-contained under the snapshot's coverage, so per-pass measurements describe different inputs.
It is off by default because it costs an extra snapshot and a full hash of it.

`range-replay-basic` runs with it on, because it is the only way the sample can tell a repeatable
replay from a replay that merely finished. Everything else a tool observes — the pass callbacks,
the `REPLAYED` status, and the application's own result — looks identical whether or not the
between-pass rewind worked.

## See also

- [`using-range-replay.rst`](../../source/docs/how-to/using-range-replay.rst) — SDK how-to for tool and application authors
- [`using-range-replay-rocprofv3.rst`](../../source/docs/how-to/using-range-replay-rocprofv3.rst) — the `rocprofv3` guide
- [`conceptual/range_replay/index.md`](../../source/docs/conceptual/range_replay/index.md) — the model, and what makes a range a candidate
- [`conceptual/range_replay/range_replay_soundness.md`](../../source/docs/conceptual/range_replay/range_replay_soundness.md) — what replay does and does not preserve
- [`samples/kernel_replay/`](../kernel_replay/README.md) — the single-dispatch service this one extends
