.. meta::
  :description: Using the ROCprofiler-SDK range replay callback tracing domain from a custom tool and marking ranges in an application
  :keywords: rocprofiler-sdk, range replay, callback tracing, RANGE_REPLAY, pass_count_cb, replay_continue_cb, replay_commit_cb, host_replay, local context

.. _using-range-replay:

==================
Using range replay
==================

Range replay lets a custom tool run **several passes over a phase of the application** -- a
sequence of dependent kernels such as one training step or one solver iteration -- inside a single
application run. Every pass starts from the same device-memory state, so a tool can collect a
different counter group, turn on thread trace, or time the phase on each pass and compare the
results directly.

Two parties cooperate:

* The **application** marks the phase with ``rocprofiler_range_replay_begin`` /
  ``rocprofiler_range_replay_end`` (or the host-begin macro).
* The **tool** subscribes to ``ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY`` and decides how many
  passes each range gets and what each pass does.

The SDK does the rest: it records the range while the application runs it (pass 0), snapshots
device memory at range entry, and when ``end`` is called, restores that snapshot and re-executes the
range once per extra pass.

This page is the SDK how-to. If you only want to collect several ``--pmc`` groups with
``rocprofv3``, see :ref:`using-range-replay-rocprofv3`. If your work is one kernel at a time, see
:ref:`using-kernel-replay`.

.. warning::

   Range replay is **experimental**. The public header is
   ``<rocprofiler-sdk/experimental/range_replay.h>``. The domain, the payload, and the entry points
   are expected to change before a stable release.

How a range runs
================

.. code-block:: text

   application                     SDK                                   your tool
   -----------                     ---                                   ---------
   rocprofiler_range_replay_begin  --> CONFIG (enter, exit)          --> set pass_count_cb, options
   kernels ...                         record + snapshot; pass 0 runs
                                       live (no PASS callback)
   rocprofiler_range_replay_end    --> commit (optional)             --> replay_commit_cb
                                       for pass 1 .. N-1:
                                         restore snapshot
                                         PASS enter                  --> toggle contexts, note pass
                                         re-execute the range
                                         PASS exit, continue?        --> replay_continue_cb
                                       CLOSE (enter, exit)           --> status, divergence
                                   <-- returns to the application

Pass 0 is the **application's own execution**. The SDK observes it rather than drives it, so it
raises no ``PASS`` callback and ``current_pass`` in ``PASS`` callbacks starts at 1. Anything you want
collected on pass 0 must already be active before ``begin``.

Quick start for tool authors
============================

Subscribe like any callback tracing domain, then answer ``CONFIG`` with a pass count. The sketch
below collects three passes per range and publishes the pass index for a dispatch-counting
callback to use:

.. code-block:: cpp

   #include <rocprofiler-sdk/experimental/range_replay.h>
   #include <rocprofiler-sdk/registration.h>

   thread_local uint64_t tl_pass = 0;  // read by your dispatch-counting callback

   uint64_t pass_count(uint64_t /*range_id*/, rocprofiler_user_data_t) { return 3; }

   void on_range_replay(rocprofiler_callback_tracing_record_t record,
                        rocprofiler_user_data_t* /*user_data*/, void* /*args*/)
   {
       if(record.kind != ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY) return;
       auto* rr = static_cast<rocprofiler_callback_tracing_range_replay_data_t*>(record.payload);
       const bool enter = (record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER);

       if(record.operation == ROCPROFILER_RANGE_REPLAY_CONFIG && enter)
           rr->pass_count_cb = pass_count;        // pass 0 (live) + 2 re-executions
       else if(record.operation == ROCPROFILER_RANGE_REPLAY_PASS)
           tl_pass = enter ? rr->current_pass : 0;
       else if(record.operation == ROCPROFILER_RANGE_REPLAY_CLOSE && enter &&
               rr->status != ROCPROFILER_RANGE_REPLAY_STATUS_REPLAYED)
           drop_range_data(rr->range_id, rr->status);  // only pass 0 ran
   }

   int tool_init(rocprofiler_client_finalize_t, void*)
   {
       rocprofiler_context_id_t ctx{};
       rocprofiler_create_context(&ctx);
       rocprofiler_configure_callback_tracing_service(
           ctx, ROCPROFILER_CALLBACK_TRACING_RANGE_REPLAY, nullptr, 0, on_range_replay, nullptr);
       rocprofiler_start_context(ctx);
       return 0;
   }

``samples/range_replay/basic_client.cpp`` is the runnable version (about 50 lines of callback plus
checks). Re-executed dispatches are submitted on the thread that called ``end`` (packet replay) or
by the application's own thread (host replay), so a ``thread_local`` pass index set in ``PASS``
enter is visible to a dispatch-counting callback for that pass -- the same pattern ``rocprofv3``
uses in ``source/lib/rocprofiler-sdk-tool/tool.cpp`` (``range_replay_callback``, ~35 lines).

The domain is **single-subscriber**: a second context configuring it gets
``ROCPROFILER_STATUS_ERROR_SERVICE_ALREADY_CONFIGURED``. Configuring it also turns on the
device-allocation tracker and HSA queue interception that replay needs; a process that never
configures the domain pays neither cost.

Quick start for application authors
===================================

Bracket the phase. The range id is yours to choose and is echoed back to the tool:

.. code-block:: cpp

   hipDeviceSynchronize();
   rocprofiler_range_replay_begin(iteration);   // tool's CONFIG runs here

   kernel_a<<<g, b, 0, stream>>>(...);           // recorded; this is pass 0
   kernel_b<<<g, b, 0, stream>>>(...);

   hipStreamSynchronize(stream);
   rocprofiler_range_replay_end();               // extra passes run here, then CLOSE

Ranges are **per thread** and do not nest; only the marking thread's dispatches are recorded.
``end`` blocks until all passes have run and returns ``ROCPROFILER_STATUS_SUCCESS`` whether the range
was replayed or declined -- the outcome goes to the tool.

.. list-table:: Return codes (``ROCPROFILER_STATUS_`` prefix omitted)
   :header-rows: 1
   :widths: 30 70

   * - Status
     - Meaning
   * - ``SUCCESS``
     - ``begin``: a range is open. ``end``: the range closed (replayed or declined).
   * - ``ERROR_CONTEXT_NOT_FOUND``
     - ``begin``: no tool configured range replay. Safe to ignore; nothing is recorded.
   * - ``ERROR_CONTEXT_NOT_STARTED``
     - ``begin``: the tool's range replay context is not active.
   * - ``ERROR_INVALID_ARGUMENT``
     - ``begin``: a range is already open on this thread. ``end``: no range is open.

To keep the application free of a link-time dependency on the SDK, resolve the two functions with
``dlsym(RTLD_DEFAULT, ...)`` as shown in :ref:`using-range-replay-rocprofv3` and
``samples/range_replay/main.cpp``. For packet replay, keep the range to one stream on one GPU,
allocate before ``begin`` and free after ``end``, keep copies and graph launches outside it, and
launch each kernel once before the first range. Ranges that need copies, several streams, or host
computation use :ref:`host replay <range-replay-howto-host>`.

.. _range-replay-howto-configure:

What you can configure
======================

Almost everything about a range's replay is a tool decision made during ``CONFIG PHASE_ENTER``.
The dials are fields of ``rocprofiler_callback_tracing_range_replay_data_t``
(``experimental/range_replay.h``, about 100 lines of documented struct), plus the ``user_data``
argument every tracing callback receives.

.. list-table::
   :header-rows: 1
   :widths: 28 16 56

   * - Dial
     - Set during
     - What it does
   * - ``pass_count_cb``
     - ``CONFIG`` enter
     - Total passes for this range, counting the live run as pass 0. ``NULL`` or ``1`` opts the range
       out; ``N > 1`` re-executes it ``N - 1`` times; ``0`` is open-ended (needs
       ``replay_continue_cb``). Receives ``range_id`` and ``user_data``, so it can differ per range.
   * - ``replay_continue_cb``
     - ``CONFIG`` enter
     - Optional. Called after each re-executed pass; return zero to stop. Stops a fixed loop early, or
       ends an open-ended one (for example, once a confidence target is met).
   * - ``host_replay``
     - ``CONFIG`` enter
     - Non-zero re-runs the application's host code for each extra pass instead of re-submitting
       recorded packets. The application must open the range with
       ``ROCPROFILER_RANGE_REPLAY_HOST_BEGIN``.
   * - ``replay_commit_cb``
     - ``CONFIG`` enter
     - Optional. Called once per range, before any extra pass, with the status the range would close
       with. Return zero to veto a replayable range (closes ``VETOED``). Used to make all MPI ranks
       agree.
   * - ``user_data``
     - ``CONFIG`` enter
     - Per-range state. The value written here is delivered to every later callback for the range,
       including ``pass_count_cb``, ``replay_continue_cb``, and ``replay_commit_cb``.
   * - ``replay_local_start_context_cb`` / ``replay_local_stop_context_cb``
     - ``PASS`` enter
     - Enable or disable one of your already-started contexts for the remaining passes of this range
       only, without touching global context state.
   * - Range placement and frequency
     - Application
     - Which code is bracketed, and how many iterations open a range. Each range pays its own
       snapshot, so bracket representative iterations rather than all of them.

The application has three helpers for host replay:

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Function
     - Purpose
   * - ``rocprofiler_range_replay_host_track_memory(ptr, size)``
     - Register host memory the range reads and writes, so it is rewound before each extra pass.
   * - ``rocprofiler_range_replay_get_host_pass(&pass)``
     - Returns 0 on the live run and 1..N-1 on re-executed passes. Use it to keep once-only side
       effects (logging, file output) on pass 0.
   * - ``ROCPROFILER_RANGE_REPLAY_HOST_BEGIN(range_id, status)``
     - Opens a host-replay range and saves the jump target in the caller's frame.

And one environment variable for development:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Variable
     - Effect
   * - ``ROCPROF_RANGE_REPLAY_VERIFY=1``
     - Hash device memory (and registered host memory) after the live run and after the final pass,
       and report how many regions differ as ``divergence_count`` on ``CLOSE``. Non-zero means the
       range reads state the snapshot does not cover. Costs an extra snapshot and a hash; turn it
       off once a range is validated.

Pass count recipes
------------------

.. list-table::
   :header-rows: 1
   :widths: 24 24 52

   * - ``pass_count_cb``
     - ``replay_continue_cb``
     - Result
   * - ``NULL`` or returns ``1``
     - ignored
     - Not replayed. ``CLOSE`` reports ``NO_PASS_COUNT`` so you can tell your own opt-out from an SDK
       decline.
   * - returns ``N > 1``
     - ``NULL``
     - Exactly ``N`` passes: the live run plus ``N - 1`` re-executions.
   * - returns ``N > 1``
     - provided
     - Up to ``N`` passes; the continue callback can stop early but not extend.
   * - returns ``0``
     - provided
     - Open-ended; runs until the continue callback returns zero. It **must** eventually return zero.
   * - returns ``0``
     - ``NULL``
     - Rejected with a warning; the range is not replayed.

Carrying state with ``user_data``
---------------------------------

Write ``user_data`` during ``CONFIG PHASE_ENTER``; every later callback for that range receives the
same value. Range replay does not make per-pass copies (unlike kernel replay), so for state that
changes from pass to pass -- sample counts, convergence data -- store a pointer in
``user_data->ptr`` and mutate the object it points to. Keep that object alive until ``CLOSE``.

Choosing what runs on each pass
-------------------------------

Configure and start every service context globally **before** the range opens. Then, during
``PASS PHASE_ENTER``, call the localized toggles to mask which contexts take part:

.. code-block:: cpp

   if(record.operation == ROCPROFILER_RANGE_REPLAY_PASS && enter)
   {
       if(rr->current_pass == 1) rr->replay_local_stop_context_cb(counters_ctx);  // sticky
       if(rr->current_pass == 2) rr->replay_local_start_context_cb(counters_ctx);
   }

* **Sticky** across passes of the range; set a context once rather than every pass.
* **Scoped** to the range: every context returns to its pre-range state at ``CLOSE``.
* **Cannot promote** a context that is globally stopped.
* **Cannot affect pass 0**, which has already run when the first ``PASS`` callback arrives.
* **Coverage varies by service:** dispatch counter collection and SPM honor both start and stop;
  kernel dispatch tracing and dispatch thread trace honor stop only; PC sampling and device counting
  ignore the toggles. The full rules are in :ref:`kernel-replay-callback-api`.

.. _range-replay-howto-host:

Host replay
===========

Packet replay re-submits only the kernels it recorded, so it declines ranges with copies, memsets,
several streams or GPUs, HIP graphs, or host computation that the kernels depend on. Host replay
handles those by re-running the application's own code between the markers.

* **Tool:** set ``rr->host_replay = 1`` in ``CONFIG PHASE_ENTER`` alongside ``pass_count_cb``.
* **Application:** open the range with ``ROCPROFILER_RANGE_REPLAY_HOST_BEGIN`` and register the host
  state the range depends on:

  .. code-block:: cpp

     rocprofiler_status_t status;
     hipDeviceSynchronize();
     rocprofiler_range_replay_host_track_memory(&host_state, sizeof(host_state));
     ROCPROFILER_RANGE_REPLAY_HOST_BEGIN(range_id, status);   // CONFIG on first entry only
     run_time_steps(&host_state);                             // kernels, copies, streams, ...
     hipDeviceSynchronize();
     rocprofiler_range_replay_end();                          // jumps back for passes 1..N-1

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Tool sets ``host_replay``
     - Application used the macro
     - Result
   * - no
     - either
     - Packet replay
   * - yes
     - yes
     - Host replay
   * - yes
     - no
     - Declined: ``HOST_JMP_NOT_SET``

Before each extra pass, the SDK restores device memory on **every** GPU plus the registered host
regions; nothing else is rewound. The application's code must stay in the same function and thread,
keep C++ objects with destructors out of scope across the jump, mark modified locals ``volatile``,
synchronize all GPU work before the macro and before ``end``, and free inside the range anything it
allocates there. The full contract and what still declines is in :ref:`range-replay-host`.

Declines at a glance
====================

A range that cannot be replayed safely is **declined, not failed**: the application's live run is
untouched, the extra passes are skipped, and ``CLOSE`` reports why in ``status``. Treat a declined
range's data as a single pass (pass 0), not a complete set.

.. list-table::
   :header-rows: 1
   :widths: 34 46 20

   * - ``ROCPROFILER_RANGE_REPLAY_STATUS_*``
     - Meaning
     - Applies to
   * - ``REPLAYED``
     - At least one extra pass ran.
     - both
   * - ``NO_PASS_COUNT``
     - The tool asked for one pass or none (opt-out).
     - both
   * - ``NO_DISPATCH``
     - The range launched no kernel on the marking thread.
     - both
   * - ``VETOED``
     - Replayable, but ``replay_commit_cb`` returned zero.
     - both
   * - ``MULTI_QUEUE`` / ``MULTI_AGENT``
     - Kernels on more than one HSA queue (HIP stream) or GPU.
     - packet
   * - ``GRAPH_LAUNCH``
     - A HIP graph was launched inside the range.
     - packet
   * - ``MEMORY_COPY_IN_RANGE``
     - An async copy wrote device memory inside the range (host replay: only from another thread).
     - both
   * - ``CONCURRENT_DISPATCH``
     - Another thread launched on the same GPU (host replay: any GPU) while the range was open.
     - both
   * - ``ALLOCATION_CHANGED_IN_RANGE``
     - A device allocation was made or freed inside the range (host replay: left unbalanced).
     - both
   * - ``CODE_OBJECT_CHANGED_IN_RANGE``
     - A code object was unloaded, or (packet) a kernel from one loaded after ``begin`` ran.
     - both
   * - ``COLLECTIVE_IN_RANGE``
     - An RCCL collective or point-to-point kernel ran inside the range.
     - both
   * - ``UNKNOWN_KERNARG_SIZE``
     - A kernel's argument size was not available (for example, hand-written assembly).
     - packet
   * - ``PROGRAM_TOO_LARGE``
     - The range recorded more dispatches than the record budget (4096).
     - packet
   * - ``SNAPSHOT_FAILED``
     - The device-memory snapshot could not be completed (usually host memory pressure).
     - both
   * - ``STAGING_FAILED``
     - Kernel-argument staging for the re-executions failed.
     - packet
   * - ``UNSUPPORTED_QUEUE_PATH``
     - Queue interposition is enabled; recorded packets cannot be re-submitted.
     - both
   * - ``HOST_JMP_NOT_SET``
     - The tool asked for host replay but the application did not use the host-begin macro.
     - host

``ROCPROFILER_RANGE_REPLAY_STATUS_LAST`` is a sentinel, not an outcome. Why each check exists, and
how cross-thread declines are detected, is in :ref:`range-replay-soundness`.

Replaying across MPI ranks
==========================

Each rank replays its own ranges. If one rank declines while others replay, anything in the range
that talks to other ranks runs a different number of times on different ranks. Use
``replay_commit_cb`` to make the decision collectively; it is called on every rank, once per range,
declined or not:

.. code-block:: cpp

   int commit_across_ranks(uint64_t, rocprofiler_range_replay_status_t status,
                           rocprofiler_user_data_t)
   {
       int local = (status == ROCPROFILER_RANGE_REPLAY_STATUS_REPLAYED);
       int all   = local;
       MPI_Allreduce(&local, &all, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
       return all;  // 0 vetoes this rank's range; ignored if it was already declined
   }

The range's device locks are held during the call, so it must not submit GPU work. Device-side
collectives (RCCL) are never replayed -- bracket the compute between communication steps. See
:ref:`range-replay-callback-api` and the multi-GPU section of :ref:`range-replay-conceptual`.

Samples and tests
=================

.. list-table::
   :header-rows: 1
   :widths: 38 62

   * - Where
     - What it shows
   * - ``samples/range_replay/basic_client.cpp`` (``range-replay-basic``)
     - The whole loop: one ``CONFIG``, passes 1-3, ``CLOSE`` with ``REPLAYED`` and zero divergence.
   * - same client, ``range-replay-host``
     - The same checks with ``host_replay`` and the ``sigsetjmp`` path in ``main.cpp``.
   * - ``samples/range_replay/opt_out_client.cpp``
     - Leaving ``pass_count_cb`` ``NULL``: the range closes ``NO_PASS_COUNT``.
   * - ``samples/range_replay/decline_client.cpp``
     - A second stream inside the range: ``CLOSE`` reports ``MULTI_QUEUE``; the application result is
       unchanged.
   * - ``tests/range-replay-local-context/``
     - Starting and stopping per-dispatch services (counters, tracing, thread trace) across replayed
       passes, with exact record counts.
   * - ``tests/range-replay-host/``
     - A realistic time-stepping solver (FFT, stencil, copies, two streams) under host replay,
       including an open-ended pass count stopped by ``replay_continue_cb``.
   * - ``tests/range-replay-concurrency/``
     - A second thread writing device memory while a range replays: every range closes exactly once,
       and no concurrent write is reverted.

See the samples README (``samples/range_replay/README.md``) for build and run commands.

Limitations
===========

* **Experimental.** API, payload, and statuses may change.
* **Single subscriber** per process.
* **Snapshot coverage:** coarse-grained device memory and module-scope ``__device__`` /
  ``__constant__`` variables (plus registered host memory in host replay). Unified / managed memory,
  ``hipMallocAsync`` pools, and fine-grained memory are not rewound. Use
  ``ROCPROF_RANGE_REPLAY_VERIFY=1`` to check a range.
* **Packet replay serializes** a pass's kernels, so concurrency-sensitive measurements can differ
  from pass 0. Host replay keeps the application's concurrency.
* **Kernels that** ``printf`` print again on re-executed passes (hostcall ``printf``).
* **Device-initiated writes to other processes** (rocSHMEM, IPC) are repeated on every pass and not
  detected; the SDK warns when peer memory is mapped.
* **A failed restore aborts the process** rather than continue on partially restored memory.

See also
========

* :ref:`using-range-replay-rocprofv3` -- the ``rocprofv3`` command-line guide
* :ref:`range-replay-sdk-api` -- operations, payload, and pass-count reference
* :ref:`range-replay-conceptual` -- how range replay works and where to go next
* :ref:`range-replay-callback-api` -- full API contract
* :ref:`range-replay-soundness` -- what makes a range replayable
* :ref:`range-replay-host` -- host replay contract
* :ref:`using-kernel-replay` -- the single-dispatch counterpart
