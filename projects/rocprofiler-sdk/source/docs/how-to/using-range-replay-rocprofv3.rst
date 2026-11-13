.. meta::
  :description: Using range replay with rocprofv3 to collect several counter groups over a marked phase of an application in a single run
  :keywords: rocprofv3, range replay, replay-mode range, range-replay-beta-enabled, range-replay-host, multi-pass counters, rocprofiler_range_replay_begin

.. _using-range-replay-rocprofv3:

====================================
Using range replay with rocprofv3
====================================

With range replay, ``rocprofv3`` collects **several hardware-counter groups over a whole phase of
your application** -- a training step, a solver iteration, a transformer layer -- in **one run**.
You mark the phase in your code; ``rocprofv3`` measures your own execution of it with the first
``--pmc`` group, then rewinds device memory to the start of the phase and re-runs it once for each
remaining group.

.. code-block:: bash

   rocprofv3 --pmc SQ_WAVES --pmc GRBM_COUNT --pmc GRBM_GUI_ACTIVE \
             --replay-mode range --range-replay-beta-enabled -- ./my_app

Use it when the kernels you care about feed each other, so measuring them one at a time
(:ref:`kernel replay <using-kernel-replay-rocprofv3>`) would not describe what the phase really
did, and re-running the whole application per group is too slow or not repeatable.

.. warning::

   Range replay is **beta**. ``--replay-mode range`` must be paired with
   ``--range-replay-beta-enabled`` to acknowledge that the flags, the SDK API
   (``<rocprofiler-sdk/experimental/range_replay.h>``), and the output may change.

Which replay should I use?
==========================

When your counters do not fit in one hardware pass, ``rocprofv3`` needs one pass per ``--pmc``
group. The three replay modes differ in *what* gets repeated:

.. list-table::
   :header-rows: 1
   :widths: 18 27 27 28

   * -
     - Application replay (default)
     - Kernel replay
     - **Range replay**
   * - What repeats
     - The whole application, once per group
     - Each kernel dispatch, in place
     - A phase you mark, after it finishes
   * - Flags
     - several ``--pmc``
     - ``--replay-mode kernel --kernel-replay-beta-enabled``
     - ``--replay-mode range --range-replay-beta-enabled``
   * - Code changes
     - None
     - None
     - Mark the phase (two calls)
   * - Pass 0 is
     - A full application run
     - Run by the profiler
     - **Your own execution** of the phase
   * - Good for
     - Short, deterministic applications
     - Independent kernels, many groups
     - Kernels that depend on each other within a phase
   * - Cost
     - ``N ×`` application runtime
     - ``N ×`` kernel time + snapshot/restore per dispatch
     - ``N ×`` phase time + one snapshot/restore per range

The modes are exclusive: ``--replay-mode`` takes one value. Range replay is not SPM, PC sampling,
or thread trace -- it only varies the counter group from one pass to the next.

Quick start
===========

#. **Mark the phase** in your application (two calls, shown below).
#. **Run** ``rocprofv3`` with two or more ``--pmc`` groups, ``--replay-mode range``, and
   ``--range-replay-beta-enabled``.
#. **Read** the counters. Each JSON counter record carries ``replay_pass``; pass ``i`` holds group
   ``i``.

Step 1: Mark the phase
----------------------

``rocprofv3`` does not choose the range -- only your application knows where a phase begins and
ends. Bracket it with ``rocprofiler_range_replay_begin`` and ``rocprofiler_range_replay_end``.

The simplest way is to look the two functions up at run time. Your application then needs only the
header (no link against ROCprofiler-SDK), and it runs unchanged without ``rocprofv3``, because the
lookup returns ``nullptr`` when the profiler is not loaded:

.. code-block:: cpp

   #include <rocprofiler-sdk/experimental/range_replay.h>
   #include <dlfcn.h>

   template <typename Fn>
   Fn rr_lookup(const char* name) { return reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, name)); }

   void step(int iter, hipStream_t stream)
   {
       static auto rr_begin = rr_lookup<decltype(&rocprofiler_range_replay_begin)>("rocprofiler_range_replay_begin");
       static auto rr_end   = rr_lookup<decltype(&rocprofiler_range_replay_end)>("rocprofiler_range_replay_end");

       hipDeviceSynchronize();                 // settle earlier work before the range opens
       if(rr_begin) rr_begin(iter);            // iter becomes the range id

       layer_norm<<<grid, block, 0, stream>>>(x, y);
       attention<<<grid, block, 0, stream>>>(y, z);
       mlp<<<grid, block, 0, stream>>>(z, x);  // each kernel reads what the previous one wrote

       hipStreamSynchronize(stream);           // the phase must be complete before it closes
       if(rr_end) rr_end();                    // extra passes run here, then it returns
   }

``samples/range_replay/main.cpp`` uses exactly this pattern (about 30 lines around the range).

A range is replayed by re-submitting the kernels it recorded (*packet replay*). For that to work,
keep the range simple:

* **One thread, one stream, one GPU.** Open and close the range on the same thread and launch its
  kernels on one stream. Ranges do not nest.
* **Allocate before, free after.** No ``hipMalloc`` / ``hipFree`` inside the range.
* **No copies or memsets into device memory inside the range** (``hipMemcpy``, ``hipMemset``), and
  no HIP graph launches. Do them before ``begin`` or after ``end``.
* **Warm up first.** Launch each kernel once before the first range, so its code object is already
  loaded.
* **Keep other threads off the GPU** while the range is open.

If your phase cannot meet these rules -- it mixes copies, several streams, graphs, or host
computation that feeds the kernels -- use :ref:`host replay <range-replay-rocprofv3-host>` instead.

You do not have to mark every iteration. Each range costs a device-memory snapshot and a restore
per extra pass, so bracket a few representative iterations and leave the rest unmarked.

Step 2: Run rocprofv3
---------------------

.. code-block:: bash

   rocprofv3 --pmc SQ_WAVES GRBM_COUNT --pmc GRBM_GUI_ACTIVE \
             --replay-mode range --range-replay-beta-enabled \
             --output-format json -- ./my_app

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Flag
     - Purpose
   * - ``--pmc <counters...>`` (repeat)
     - One counter group per ``--pmc``. The number of groups sets the number of passes. Required.
   * - ``--replay-mode range``
     - Select range replay instead of application replay.
   * - ``--range-replay-beta-enabled``
     - Acknowledge the beta. Required with ``--replay-mode range``.
   * - ``--range-replay-host``
     - Re-run your host code for each extra pass instead of re-submitting recorded kernels. Your
       application must open the range with ``ROCPROFILER_RANGE_REPLAY_HOST_BEGIN``. Requires
       ``--replay-mode range``.

``rocprofv3`` rejects the run before launching your application if ``--replay-mode range`` is
missing ``--range-replay-beta-enabled`` or counter collection, if ``--range-replay-host`` is given
without ``--replay-mode range``, or if ``--att``, ``--spm``, or PC sampling is also requested.

Each ``--pmc`` group must fit in one hardware pass on its own; replay does not split a group. Check
a group first with ``rocprofv3-avail pmc-check`` (see :ref:`using-rocprofv3-avail`).

Step 3: Read the output
-----------------------

See :ref:`range-replay-rocprofv3-output` below.

How passes map to counter groups
================================

Pass 0 is **your application's own execution** of the range. ``rocprofv3`` does not run it a
second time; it measures it with the first group. Each extra pass then restores device memory to the
range's entry state, re-runs the range, and collects the next group.

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Pass
     - Who runs it
     - Counter group collected
   * - 0
     - Your application (live)
     - First ``--pmc`` group
   * - 1 .. N-1
     - ``rocprofv3``, after ``end`` is called
     - The next group in order: pass 1 collects the second ``--pmc``, pass 2 the third, and so on

A few consequences worth knowing:

* **The pass count is the number of** ``--pmc`` **groups.** There is no separate pass-count flag.
  On a system with several GPU models, it is the most groups any GPU can collect.
* **Kernels outside any range run once and collect the first group only.** If you need every group
  for those kernels too, bracket them or use application replay.
* **A declined range also collects only the first group.** Your application still runs correctly;
  the extra passes are just skipped. See :ref:`range-replay-rocprofv3-declines`.
* **Your application sees its own results.** After the last pass, device memory is left exactly as
  your own execution of the range produced it (packet replay) or as the final re-run produced it
  (host replay, which is the same for a deterministic range).
* ``rocprofiler_range_replay_end`` **blocks** until all passes have run, so a replayed range takes
  roughly ``N`` times longer to close.

.. _range-replay-rocprofv3-host:

Host replay: phases with copies, streams, and host code
=======================================================

Packet replay re-submits only the recorded kernels. A real time step usually does more: copies,
memsets, a second stream, a HIP graph, a scratch allocation, host computation whose result the
next kernel reads. Packet replay declines such ranges. **Host replay** handles them by re-running
your own code between the markers for each extra pass.

Enable it with ``--range-replay-host`` and open the range with the macro, which saves a jump target
in your function:

.. code-block:: cpp

   #include <rocprofiler-sdk/experimental/range_replay.h>
   #include <setjmp.h>

   void solve(State* host_state)       // host_state: plain data the range reads and writes
   {
       rocprofiler_status_t status;
       hipDeviceSynchronize();
       rocprofiler_range_replay_host_track_memory(host_state, sizeof(*host_state));
       ROCPROFILER_RANGE_REPLAY_HOST_BEGIN(/*range_id=*/42, status);

       for(int s = 0; s < 10; ++s)
           time_step(host_state, s);  // kernels, copies, memsets, streams, host math ...

       hipDeviceSynchronize();
       rocprofiler_range_replay_end(); // jumps back to the macro for each extra pass
   }

.. code-block:: bash

   rocprofv3 --pmc SQ_WAVES --pmc GRBM_COUNT \
             --replay-mode range --range-replay-beta-enabled --range-replay-host -- ./my_app

The macro calls the SDK directly, so this form links ROCprofiler-SDK into your application. The
in-tree samples avoid that (linking the SDK into a HIP executable has caused
``hipErrorInvalidDeviceFunction`` on the first launch on gfx942) and spell the macro out with the
``dlsym`` lookup from Step 1:

.. code-block:: cpp

   static auto rr_jmpbuf = rr_lookup<void* (*)()>("rocprofiler_range_replay_host_jmpbuf");

   volatile rocprofiler_status_t status = ROCPROFILER_STATUS_SUCCESS;
   if(sigsetjmp(*static_cast<sigjmp_buf*>(rr_jmpbuf()), 1) == 0)  // must be the whole if-condition
       status = rr_begin(42);                                    // rr_begin from Step 1

Look up ``rocprofiler_range_replay_host_track_memory`` and ``rocprofiler_range_replay_get_host_pass``
the same way if you use them. ``samples/range_replay/main.cpp`` (``RR_APP_MODE=host``) and ``tests/range-replay-host/main.cpp``
show the complete form; :ref:`range-replay-host` explains why the jump target has to live in your
function.

Host replay rewinds device memory on every GPU and the host memory you register; nothing else.
Your code between the markers must follow a short contract:

* Same thread and same function from the macro to ``end``.
* No C++ object with a destructor (``std::vector``, ``std::string``) may be live across the jump --
  put the range body in its own function, as ``time_step`` is above.
* Locals of the enclosing function that the range modifies must be ``volatile`` or re-initialized
  inside the range.
* Register host data the range reads and writes with
  ``rocprofiler_range_replay_host_track_memory``.
* Synchronize **all** GPU work (including async copies) before the macro and before ``end``.
* Allocations inside the range must be freed inside it.
* Keep once-only side effects (printing, file writes) on pass 0: call
  ``rocprofiler_range_replay_get_host_pass`` and skip them when it reports a pass other than 0.

If ``--range-replay-host`` is given but your application uses the plain ``begin`` call, the range
runs once and is declined with ``HOST_JMP_NOT_SET``.

What you can configure
======================

The CLI deliberately exposes a small surface: you choose the groups and where the range is, and
``rocprofv3`` does the rest.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - You control
     - How
   * - Which counters, and how many passes
     - One ``--pmc`` per group (or ``pmc_groups`` / ``pmc`` in an input file). Passes = groups.
   * - Which phase is replayed
     - Where your application calls ``begin`` / ``end``.
   * - How many ranges are replayed
     - How many times your application opens a range. Mark only the iterations you want measured.
   * - Packet or host replay
     - Default is packet replay; add ``--range-replay-host`` and use the host-begin macro for host
       replay.
   * - Output and other rocprofv3 options
     - As usual (``--output-format``, ``-d``, ``-o``, and so on). Input-file jobs run as they would
       without replay, and replay applies to the counter groups each job asks for. See the note on
       kernel filters under `Limitations`_.

What the CLI does **not** expose -- because it is a tool-author decision -- is available through the
SDK (:ref:`using-range-replay`): a custom pass count per range, an early stop
(``replay_continue_cb``), a cross-process commit (``replay_commit_cb``) for MPI jobs, per-pass
service toggles (for example, thread trace on one pass), and per-range opt-out.

.. _range-replay-rocprofv3-output:

Output
======

All groups from one range-replay run are written together, in the same output directory, as a
single run.

* **JSON** counter records include ``replay_pass`` (0-based). Pass 0 is your own execution and
  holds the first ``--pmc`` group; pass 1 holds the second, and so on. Unlike kernel replay, each re-executed dispatch gets its **own**
  dispatch id, so use ``replay_pass`` (not ``dispatch_id``) to tell passes apart.
* **CSV** ``counter_collection.csv`` has the usual columns and no ``Replay_Pass`` column. When the
  groups request different counters, ``Counter_Name`` tells you the pass; otherwise add
  ``--output-format json``.
* **rocpd** (the default) can be converted as described in :ref:`using-rocpd-output-format`.

.. _range-replay-rocprofv3-declines:

Why was my range not replayed?
==============================

Range replay **declines rather than fails**. If a range cannot be replayed safely, your
application's own execution still runs and completes normally; ``rocprofv3`` just skips the extra
passes, so that range has data for the first group only.

How to tell: the range's dispatches have no records with ``replay_pass`` greater than 0. With
``--log-level info``, the SDK also logs a ``range replay: range <id> (<n> dispatches) phases: ...``
timing line for each packet-replay range it did replay; a range without that line was declined.

``rocprofv3`` does not print the decline reason itself, so use the table below as a checklist. Each
row is named after the status a custom tool receives in its ``CLOSE`` callback
(:ref:`using-range-replay`), which is the way to get the exact reason.

.. list-table:: Common reasons and fixes
   :header-rows: 1
   :widths: 32 68

   * - Reason (``CLOSE`` status)
     - Fix
   * - ``MULTI_QUEUE`` / ``MULTI_AGENT``
     - Launch the range's kernels on one stream and one GPU, or use ``--range-replay-host``.
   * - ``MEMORY_COPY_IN_RANGE``
     - Move ``hipMemcpy`` / ``hipMemset`` outside the range, or use ``--range-replay-host``.
   * - ``GRAPH_LAUNCH``
     - Launch graphs outside the range, or use ``--range-replay-host``.
   * - ``ALLOCATION_CHANGED_IN_RANGE``
     - Allocate before ``begin`` and free after ``end``. With host replay, free inside the range
       anything you allocate inside it; warm up libraries that allocate lazily (FFT plans, BLAS
       workspaces).
   * - ``CODE_OBJECT_CHANGED_IN_RANGE``
     - Launch each kernel once before the first range (warm-up).
   * - ``CONCURRENT_DISPATCH``
     - Keep other threads from launching on the same GPU while the range is open.
   * - ``COLLECTIVE_IN_RANGE``
     - RCCL kernels are never replayed. Bracket the compute between communication steps.
   * - ``HOST_JMP_NOT_SET``
     - ``--range-replay-host`` was given, but the range was opened with plain ``begin``. Use
       ``ROCPROFILER_RANGE_REPLAY_HOST_BEGIN``.
   * - ``NO_DISPATCH``
     - The range launched no kernel on the marking thread.
   * - ``SNAPSHOT_FAILED``
     - Not enough host memory to hold the device snapshot. Shrink the working set or the range.

The full list, with why each condition is checked, is in :ref:`range-replay-soundness`.

Limitations
===========

* **Beta.** Flags, API, and output may change.
* **Counters only.** ``--att``, ``--spm``, and PC sampling cannot be combined with
  ``--replay-mode range``. Collect them in a separate run.
* **Requires two or more** ``--pmc`` **groups** to replay anything; with one group the range runs
  once.
* **Your application must mark the range.** An application that never calls the range API runs
  once and collects the first group only.
* **Packet replay serializes kernels** within each extra pass, so concurrency-sensitive counters can
  differ from pass 0. Host replay runs streams as your code does.
* **What is rewound:** coarse-grained device memory (``hipMalloc``) and module-scope
  ``__device__`` / ``__constant__`` variables, plus registered host memory in host replay. Unified /
  managed memory, ``hipMallocAsync`` pools, fine-grained and host memory are not rewound; a range
  that depends on them measures different inputs on each pass.
* **Kernels that** ``printf`` print again on each extra pass (HIP's default hostcall ``printf``).
* **Kernel filters.** ``--kernel-include-regex``, ``--kernel-exclude-regex``, and
  ``--kernel-iteration-range`` were designed around kernel replay. In this beta, prefer choosing what
  you measure by where you place the range, rather than relying on filters inside replayed ranges.
* **Multi-process jobs.** Each rank replays its ranges independently; ``rocprofv3`` does not agree
  across ranks. Keep host collectives (for example ``MPI_Allreduce``) out of ranges. A custom tool can
  agree across ranks with ``replay_commit_cb`` (:ref:`using-range-replay`).
* **Host memory** for the snapshot scales with the GPU's tracked device footprint (all GPUs for host
  replay).

See also
========

* :ref:`using-range-replay` -- SDK how-to for tool authors and application authors
* :ref:`range-replay-sdk-api` -- payload, operations, and statuses
* :ref:`range-replay-conceptual` -- how range replay works, carve-outs, multi-GPU
* :ref:`range-replay-host` -- the full host-replay contract
* :ref:`using-kernel-replay-rocprofv3` -- per-dispatch replay, no code changes
* ``samples/range_replay/`` -- runnable application and tools
