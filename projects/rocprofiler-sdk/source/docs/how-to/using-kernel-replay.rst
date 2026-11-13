.. meta::
  :description: Using the ROCprofiler-SDK kernel replay callback tracing domain from a custom tool
  :keywords: rocprofiler-sdk, kernel replay, callback tracing, KERNEL_REPLAY, replay_pass_count, replay_continue, local context

.. _using-kernel-replay:

===================
Using kernel replay
===================

Kernel replay lets a custom tool **run the same GPU dispatch several times inside one application
run**, with device memory restored between runs so every pass sees identical inputs. The
application never notices: it observes one kernel completion and the memory its kernel produced.

With it, a tool can:

* collect **more counter groups** than fit in one hardware pass, all on the same dispatch;
* put **different services on different passes** -- counters on one pass, thread trace or SPM on
  another -- so they do not interfere;
* **choose per dispatch** whether to replay at all, and how many times;
* **stop early or loop adaptively** until it has the data it needs.

Kernel replay is a **callback tracing domain**, not a counting mode, so it works with any per-pass
work you like. ``rocprofv3`` uses it to collect several ``--pmc`` groups in one run
(:ref:`using-kernel-replay-rocprofv3`). If the work you want to measure is a *sequence* of kernels
that feed each other, use :ref:`range replay <using-range-replay>` instead.

.. warning::

   Kernel replay is **experimental**. The public header is
   ``<rocprofiler-sdk/experimental/kernel_replay.h>``. The domain, payload, and limitations below
   are expected to change before a stable release.

How a replayed dispatch runs
============================

.. code-block:: text

   application submits a kernel
     -> CONFIG enter     your tool sets replay_pass_count (and optionally replay_continue, user_data)
     -> SDK asks replay_pass_count(dispatch_info, user_data) -> N
     -> snapshot device memory
     -> for pass 0 .. N-1:
          restore snapshot (not before pass 0)
          PASS enter     your tool notes current_pass, toggles contexts
          kernel runs    your services collect as usual
          PASS exit      your tool may override replay_continue for this pass
          continue?      replay_continue(...) decides whether another pass follows
     -> CONFIG exit
   application sees one completion

Everything happens synchronously on the thread that submitted the dispatch. One dispatch id is
reserved before ``CONFIG`` and reused for every pass, so distinguish passes by ``current_pass``.

Quick start
===========

Subscribe through ordinary callback tracing (there is no dedicated configure function), then
answer ``CONFIG`` with a pass-count callback:

.. code-block:: cpp

   #include <rocprofiler-sdk/experimental/kernel_replay.h>

   thread_local uint64_t tl_pass = 0;  // read by your dispatch-counting callback

   uint64_t pass_count(rocprofiler_kernel_dispatch_info_t info, rocprofiler_user_data_t)
   {
       return groups_for_agent(info.agent_id);  // 1 = do not replay this dispatch
   }

   void on_kernel_replay(rocprofiler_callback_tracing_record_t record,
                         rocprofiler_user_data_t* /*user_data*/, void* /*args*/)
   {
       if(record.kind != ROCPROFILER_CALLBACK_TRACING_KERNEL_REPLAY) return;
       auto* kr = static_cast<rocprofiler_callback_tracing_kernel_replay_data_t*>(record.payload);
       const bool enter = (record.phase == ROCPROFILER_CALLBACK_PHASE_ENTER);

       if(record.operation == ROCPROFILER_KERNEL_REPLAY_CONFIG && enter)
           kr->replay_pass_count = pass_count;
       else if(record.operation == ROCPROFILER_KERNEL_REPLAY_PASS)
           tl_pass = enter ? kr->current_pass : 0;  // 0-based; total_passes is N (0 if open-ended)
   }

   // in tool_init:
   rocprofiler_context_id_t ctx{};
   rocprofiler_create_context(&ctx);
   rocprofiler_configure_callback_tracing_service(
       ctx, ROCPROFILER_CALLBACK_TRACING_KERNEL_REPLAY, nullptr, 0, on_kernel_replay, nullptr);
   rocprofiler_start_context(ctx);

Configure dispatch counting as usual on another context; in its dispatch callback, pick the counter
config for ``tl_pass``. ``samples/kernel_replay/counters_client.cpp`` does exactly this in about 40
lines of callback code.

Configuring the domain also turns on the device-allocation tracker used for snapshot and restore;
a process that never configures it pays nothing. Only one context may subscribe -- a second
configure call returns ``ROCPROFILER_STATUS_ERROR_SERVICE_ALREADY_CONFIGURED``.

What you can configure
======================

The dials are fields of ``rocprofiler_callback_tracing_kernel_replay_data_t``
(``experimental/kernel_replay.h``, about 70 lines of documented struct), plus the ``user_data``
argument every tracing callback receives. There is no environment variable or global setting: all
replay policy is decided by your tool, per dispatch.

.. list-table::
   :header-rows: 1
   :widths: 26 18 56

   * - Dial
     - Set during
     - What it does
   * - ``replay_pass_count``
     - ``CONFIG`` enter
     - Callback returning the pass count for this dispatch. Receives ``dispatch_info`` (agent,
       kernel id, grid and workgroup sizes, ...) and ``user_data``, so the count can differ per kernel
       or per GPU. ``NULL`` or ``1`` means "do not replay".
   * - ``replay_continue``
     - ``CONFIG`` enter (default); ``PASS`` exit (override)
     - Optional callback after each pass; return zero to stop. Stops a fixed loop early or ends an
       open-ended one. Overriding it during ``PASS PHASE_EXIT`` applies to that pass only.
   * - ``user_data``
     - ``CONFIG`` enter
     - Per-dispatch state delivered to ``replay_pass_count``, every ``PASS`` callback, and
       ``replay_continue``. Each pass gets its own copy (see below).
   * - ``replay_start_context`` / ``replay_stop_context``
     - ``PASS`` enter
     - Enable or disable one of your already-started contexts for this replay loop only, without
       touching global context state.
   * - Per-dispatch opt-out
     - ``CONFIG`` enter
     - Leave ``replay_pass_count`` ``NULL`` (or return ``1``) for dispatches you do not want replayed;
       they run once with no snapshot.

Pass count recipes
------------------

.. list-table::
   :header-rows: 1
   :widths: 22 22 56

   * - ``replay_pass_count``
     - ``replay_continue``
     - Behavior
   * - left ``NULL``
     - ignored
     - Not replayed. Ordinary path, no snapshot.
   * - returns ``1``
     - ignored
     - Not replayed. One pass needs no snapshot.
   * - returns ``N > 1``
     - ``NULL``
     - Exactly ``N`` passes.
   * - returns ``N > 1``
     - provided
     - Up to ``N`` passes; the continue callback can stop early but not extend. It sees
       ``current_pass`` 0 through ``N-2``.
   * - returns ``0``
     - provided
     - Open-ended; runs until the continue callback returns zero. It **must** eventually return
       zero on every path -- the application's completion is held until the loop ends.
   * - returns ``0``
     - ``NULL``
     - Rejected: the SDK warns and the dispatch is not replayed.

Replay only some dispatches
---------------------------

Decide in ``CONFIG PHASE_ENTER`` from ``dispatch_info``. This is the whole policy in
``samples/kernel_replay/opt_out_client.cpp``:

.. code-block:: cpp

   if(record.operation == ROCPROFILER_KERNEL_REPLAY_CONFIG && enter)
   {
       if(kr->dispatch_info.workgroup_size.x == 67)  // the kernel we care about
           kr->replay_pass_count = pass_count;
       // otherwise leave it NULL: this dispatch runs once, no snapshot
   }

Stop early or loop adaptively
-----------------------------

Set ``replay_continue`` next to ``replay_pass_count``. Return non-zero to run another pass:

.. code-block:: cpp

   int keep_going(rocprofiler_kernel_dispatch_info_t, uint64_t current_pass,
                  uint64_t /*total_passes*/, rocprofiler_user_data_t)
   {
       return current_pass < 1 ? 1 : 0;  // stop after pass 1 even if 4 were requested
   }
   // in CONFIG enter: kr->replay_pass_count = four_passes; kr->replay_continue = keep_going;

``samples/kernel_replay/early_exit_client.cpp`` is this example; return ``0`` from
``replay_pass_count`` instead for an open-ended loop.

Carry state with ``user_data``
------------------------------

The tracing callback receives a ``rocprofiler_user_data_t*`` for the current dispatch. Write it
during ``CONFIG PHASE_ENTER`` and the SDK hands it to ``replay_pass_count``, every ``PASS``
callback, and ``replay_continue``. Each pass gets **its own copy**, re-seeded from the ``CONFIG``
value, so a write during a pass stays with that pass:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Observed from
     - Value delivered
   * - ``replay_pass_count``
     - the ``CONFIG PHASE_ENTER`` value
   * - ``PASS PHASE_ENTER``
     - the ``CONFIG PHASE_ENTER`` value, on every pass
   * - ``PASS PHASE_EXIT``
     - whatever this pass's ``PHASE_ENTER`` left
   * - ``replay_continue``
     - whatever this pass's ``PHASE_EXIT`` left
   * - ``CONFIG PHASE_EXIT``
     - the ``CONFIG PHASE_ENTER`` value

Two useful consequences:

* A ``PASS PHASE_EXIT`` callback can decide "stop now" and hand that to ``replay_continue`` by
  writing ``user_data->value`` -- the decision applies to this pass only.
* State that must accumulate across passes (sample counts, convergence data) belongs behind
  ``user_data->ptr``: the pointer is re-seeded every pass, but the object it points to is shared.

``samples/kernel_replay/basic_client_with_user_data.cpp`` demonstrates both and asserts every row of
the table.

Different services on different passes
--------------------------------------

Configure and start each service on its own context **before** replay begins. Then, during
``PASS PHASE_ENTER``, use the localized toggles to choose which contexts take part. From
``samples/kernel_replay/att_client.cpp`` (counters on pass 0, thread trace on pass 1):

.. code-block:: cpp

   if(record.operation == ROCPROFILER_KERNEL_REPLAY_PASS && enter)
   {
       const bool counters = (kr->current_pass != 1);
       (counters ? kr->replay_start_context : kr->replay_stop_context)(counters_ctx);
       (counters ? kr->replay_stop_context : kr->replay_start_context)(att_ctx);
   }

* **Only valid during** ``PASS PHASE_ENTER``; elsewhere they return
  ``ROCPROFILER_STATUS_ERROR_CONTEXT_ERROR``.
* **Sticky** across passes of this dispatch, and **scoped** to the replay loop: every context
  returns to its pre-replay state afterwards.
* **Cannot promote** a context that is globally stopped.
* **Coverage varies by service.** Dispatch counter collection and SPM honor start and stop. Kernel
  dispatch tracing and dispatch thread trace honor stop only. PC sampling and device counting ignore
  the toggles (the call succeeds but has no effect), so PC sampling cannot be isolated to one pass.
* **Keep incompatible services on separate passes.** Counters and PC sampling conflict on MI2xx /
  MI3xx (clock gating); ATT and SPM both inject packets around the dispatch.

Do not call the global ``rocprofiler_start_context`` / ``rocprofiler_stop_context`` from inside
the replay loop -- that leaks into non-replayed dispatches. The full contract is in
:ref:`kernel-replay-callback-api`.

What is snapshotted
===================

Between passes, kernel replay restores:

* Coarse-grained device (VRAM) allocations from ``hipMalloc`` /
  ``hsa_amd_memory_pool_allocate`` / ``hsa_memory_allocate`` owned by the dispatch's GPU.
* Module-scope ``__device__`` and ``__constant__`` variables in loaded executables.

It does **not** restore unified or managed memory, ``hipMallocAsync`` and other pool-backed
allocations, host, fine-grained, kernarg, or executable allocations, or HIP graph-managed memory.
A kernel that writes those sees values accumulated across passes.

The snapshot covers the GPU's **whole tracked footprint**, not only what the kernel touches, and is
a full in-memory copy to host RAM: cost is ``O(tracked_bytes × passes)``. The last pass is not
restored, so the application continues with the memory its kernel actually produced. See
:ref:`kernel-replay-memory-snapshot` and :ref:`kernel-replay-performance`.

.. _kernel-replay-limitations:

Limitations
===========

* **Experimental.** The domain, payload, and snapshot policy may change.
* **Single** ``KERNEL_REPLAY`` **subscriber** per process.
* **Snapshot coverage** is coarse-grained device memory plus module-scope device and constant
  variables (above).
* **HIP graph launches are not replayed.** The SDK warns once and the graph runs once on the
  ordinary path.
* **Only single-packet, single-dispatch submissions** are replayed. Multi-packet batches warn once
  and run once.
* **The GPU is serialized** for the replay window by a per-agent lock. Different GPUs replay
  concurrently, but there is no in-tree multi-GPU test and no MPI coordination.
* **Async copies are not fenced.** ``hsa_amd_memory_async_copy`` (or HIP async memcpy) on another
  thread can change device memory during the replay window.
* **Stuck drains abort the process** rather than hang (roughly 60 s bounds inside the window), and
  a failed restore aborts rather than continue on partially restored memory.
* **Host RAM** holds a copy of the tracked device footprint during replay. Under host memory
  pressure the snapshot is declined and the dispatch runs once.

Samples and tests
=================

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Where
     - What it shows
   * - ``samples/kernel_replay/basic_client.cpp``
     - Four passes, replay only. Start here.
   * - ``samples/kernel_replay/counters_client.cpp``
     - A different counter group on each pass.
   * - ``samples/kernel_replay/opt_out_client.cpp``
     - Replaying one kernel and leaving another alone.
   * - ``samples/kernel_replay/early_exit_client.cpp``
     - ``replay_continue`` stopping after pass 1 of 4.
   * - ``samples/kernel_replay/basic_client_with_user_data.cpp``
     - ``user_data`` semantics, including a ``PASS``-driven stop.
   * - ``samples/kernel_replay/att_client.cpp`` / ``spm_client.cpp``
     - Counters on pass 0, thread trace or SPM on pass 1.
   * - ``tests/kernel-replay-local-context/``
     - Per-service start/stop across passes, with exact record counts.
   * - ``tests/kernel-replay-concurrency/``
     - A replayed kernel next to an unreplayed one: snapshot/restore does not corrupt concurrent
       work.

Build and run commands are in ``samples/kernel_replay/README.md``.

See also
========

* :ref:`using-kernel-replay-rocprofv3` -- the ``rocprofv3`` command-line guide
* :ref:`kernel-replay-sdk-api` -- payload, operations, and the dispatch-counting pattern
* :ref:`kernel-replay-conceptual` -- how kernel replay works and where to go next
* :ref:`kernel-replay-callback-api` -- full API contract
* :ref:`using-range-replay` -- replaying a sequence of dependent kernels
