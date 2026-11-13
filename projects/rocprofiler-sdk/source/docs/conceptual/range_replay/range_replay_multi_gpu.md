(range-replay-multi-gpu)=
# Range replay — Multi-GPU and collectives

Each MPI rank runs its own range in its own process. Snapshots are agent-scoped and per-process,
so ranks replay independently; what they have to share is the decision.

**Agreement.** A rank that declines runs its range once, while one that replays runs it again.
Anything in the range that talks to other ranks -- a host `MPI_Allreduce` in a host-replay range,
for example -- then runs a different number of times on different ranks. `replay_commit_cb` closes
that gap: it is delivered once per range on every rank, declined or not, after eligibility is final
and before anything is rewound, so a tool can allreduce the status and veto unless every rank can
replay. Vetoed ranges close with `VETOED`. See
[Committing to a replay](range_replay_callback_api.md#committing-to-a-replay).

**What agreement makes replayable.** Host-side communication on host buffers inside a host-replay
range (staged D2H copy, `MPI_Allreduce`, H2D copy), because every pass re-runs it on every rank in
the same order. Pass counts must also agree; a fixed `pass_count_cb` does that, and an open-ended
`replay_continue_cb` must reduce its decision across ranks the same way.

**What it does not.** Device-side communication. RCCL kernels keep protocol state in device memory
the snapshot rewinds and read flags in peer and fine-grained memory it does not, so replaying them is
unsound on every rank, and ranges that dispatch them decline with `COLLECTIVE_IN_RANGE`. User
kernels that write another process's memory (rocSHMEM puts and atomics) are not recognizable and
are re-executed by every pass; the SDK warns once when the process maps peer memory. See
[Communication kernels](range_replay_soundness.md#communication-kernels).

**What to bracket.** The compute between communication steps. A range around Jacobi sweeps and a
matmul in an RCCL or MPI job replays on every rank, and the communicator keeps working afterwards.
Inter-node collectives add host proxy threads and NIC state that no device snapshot rewinds, which
is another reason to keep communication outside the range.

**rocprofv3.** `rocprofv3 --replay-mode range` does not set `replay_commit_cb`, so each rank decides
on its own. Keep host collectives out of ranges when profiling an MPI job with the CLI, or write a
custom tool that commits across ranks ({ref}`using-range-replay`).

## Measured behavior

Measured on one node with eight MI325X GPUs, Open MPI 4.1.2 (not GPU-aware) and RCCL, one GPU
per rank, four passes with `SQ_WAVES`, verification on, and five full iterations after the range to
check that the communicator still works:

| Range contents | Mode | All ranks eligible | One rank declines, no agreement | One rank declines, with agreement |
|---|---|---|---|---|
| Jacobi + matmul, `MPI_Allreduce` after the range | packet | `REPLAYED` | others `REPLAYED` | others `VETOED` |
| same | host | `REPLAYED` | others `REPLAYED` | others `VETOED` |
| + D2H copy, host `MPI_Allreduce`, H2D copy | packet | `MEMORY_COPY_IN_RANGE` | `MEMORY_COPY_IN_RANGE` | `MEMORY_COPY_IN_RANGE` |
| same | host | `REPLAYED` | `MPI_ERR_TRUNCATE` abort | others `VETOED` |
| + `ncclAllReduce` on device buffers | packet, host | `COLLECTIVE_IN_RANGE` | others `COLLECTIVE_IN_RANGE` | others `COLLECTIVE_IN_RANGE` |
| Jacobi + matmul, `ncclAllReduce` right after the range | packet | `REPLAYED` | others `REPLAYED` | others `VETOED` |
| same | host | `REPLAYED` | others `REPLAYED` | others `VETOED` |

Every run produced the application's own result on every rank, on 2, 4 and 8 ranks, except the
one marked as an abort. The declining rank reports its own reason. RCCL runs a different kernel on
eight ranks (`meta::comms::ddaAllReduceFlatIpc`) than on four (`ncclDevKernel_Generic`); both
decline. A rocSHMEM range whose kernel adds to a neighbour's counter replays in both
modes and leaves every counter at 4 instead of 1; that is the case the warning is for.
