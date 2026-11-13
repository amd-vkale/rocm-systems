.. meta::
  :description: Using kernel replay with rocprofv3 to collect multiple counter groups in a single application run
  :keywords: rocprofv3, kernel replay, replay-mode, kernel-replay-beta-enabled, multi-pass counters, application replay, snapshot restore

.. _using-kernel-replay-rocprofv3:

======================================
Using kernel replay with rocprofv3
======================================

With kernel replay, ``rocprofv3`` collects **several hardware-counter groups in one run of your
application**, with no code changes. Each kernel dispatch is run once per ``--pmc`` group, and
device memory is restored between runs so every group measures the same work:

.. code-block:: bash

   rocprofv3 --pmc SQ_WAVES GRBM_COUNT --pmc GRBM_GUI_ACTIVE \
             --replay-mode kernel --kernel-replay-beta-enabled -- ./my_app

Without ``--replay-mode kernel``, several ``--pmc`` groups use *application replay*: the whole
application is re-run once per group. Kernel replay is the better choice when those re-runs are
slow or not repeatable.

This page is the command-line guide. Tool authors who want to control passes themselves should read
:ref:`using-kernel-replay`.

.. warning::

   Kernel replay is **beta**. ``--replay-mode kernel`` must be paired with
   ``--kernel-replay-beta-enabled`` to acknowledge that the flags, the SDK API
   (``<rocprofiler-sdk/experimental/kernel_replay.h>``), and the output may change.

Which replay should I use?
==========================

Hardware has a limited number of counter registers per block. When your counters do not fit in one
hardware pass, ``rocprofv3`` can collect them in several ways:

.. list-table::
   :header-rows: 1
   :widths: 22 28 25 25

   * - Approach
     - What repeats
     - Memory handling
     - Cost
   * - Application replay (default for several ``--pmc`` groups)
     - Whole application, once per group
     - None; each run is a fresh process
     - ``N ×`` application runtime
   * - **Kernel replay** (``--replay-mode kernel``)
     - Each dispatch, in place
     - Device memory snapshot and restore between passes
     - ``N ×`` kernel time + snapshot/restore per dispatch
   * - Range replay (``--replay-mode range``)
     - A phase your application marks
     - The same snapshot, taken once per range
     - ``N ×`` phase time + snapshot/restore per range
   * - Counter group rotation (``pmc_groups`` / ``pmc_group_interval``)
     - Nothing; successive dispatches sample different groups
     - None
     - ``1 ×`` application runtime, but not the same dispatch

Choose **range replay** (:ref:`using-range-replay-rocprofv3`) when the kernels you care about feed
each other within a phase and you can mark that phase in your code. Kernel replay is **not**
:ref:`SPM <using-spm>`: SPM streams counter samples over time; kernel replay re-executes a dispatch
so each pass can collect a different group against the same inputs.

Quick start
===========

#. **List and check your counters.** ``rocprofv3 --list-avail`` (or ``rocprofv3-avail list --pmc``)
   shows what your GPU supports; ``rocprofv3-avail pmc-check <counters...>`` confirms a group fits
   in one hardware pass. See :ref:`using-rocprofv3-avail`.
#. **Run with one** ``--pmc`` **per group**, plus ``--replay-mode kernel
   --kernel-replay-beta-enabled``:

   .. code-block:: bash

      rocprofv3 --pmc SQ_WAVES GRBM_COUNT --pmc GRBM_GUI_ACTIVE \
                --replay-mode kernel --kernel-replay-beta-enabled \
                --output-format json -- ./my_app

#. **Read the output.** Each counter record carries ``replay_pass``: pass 0 collected ``SQ_WAVES``
   and ``GRBM_COUNT``, pass 1 collected ``GRBM_GUI_ACTIVE``. Both come from the same dispatch.

``rocprofv3`` rejects the run before launching your application if ``--kernel-replay-beta-enabled``
or counter collection is missing, or if ``--att``, ``--spm``, or PC sampling is also requested.

What you can configure
======================

The CLI is deliberately simple: you choose the groups and which kernels to measure, and
``rocprofv3`` derives everything else.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - You control
     - How
   * - Which counters, and how many passes
     - One ``--pmc`` per group (or ``pmc`` / ``pmc_groups`` in an input file). Passes = groups.
   * - Which kernels report counters
     - The usual filters: ``--kernel-include-regex``, ``--kernel-exclude-regex``,
       ``--kernel-iteration-range`` (see :ref:`kernel-naming-filtering`). An iteration counts the
       application's launch once, not once per pass.
   * - Output format and location
     - ``--output-format`` (``json`` recommended; see `Output`_), ``-d``, ``-o``.
   * - Several configurations in one invocation
     - Input-file jobs. Each job runs exactly as it would without replay, with its own output,
       filters, and ranges; replay applies to the counter groups that job asks for.
   * - Beta acknowledgement
     - ``--kernel-replay-beta-enabled``. Required.

Filters choose which dispatches **report** counters. In this beta every dispatch on a GPU with two
or more collectable groups is still replayed, so filters do not reduce replay cost.

The following are **not** CLI options, by design; they are available to custom tools through the
SDK (:ref:`using-kernel-replay`):

* a pass count other than the number of groups (there is no ``--kernel-replay-passes`` flag or
  environment variable);
* skipping replay for selected kernels (per-dispatch opt-out);
* stopping early or looping adaptively (``replay_continue``);
* running a different service, such as thread trace or SPM, on selected passes.

How passes map to counter groups
================================

The pass count is not a number you supply. ``rocprofv3`` derives it per dispatch from the number of
counter groups collectable on **that dispatch's GPU**, and pass ``i`` collects group ``i``. A GPU that
can collect fewer groups than the ``--pmc`` list is replayed only as many times as it has groups, so
pass and group stay aligned.

With a single ``--pmc`` group, the tool asks for one pass, which takes the ordinary path with no
snapshot or restore:

.. code-block:: bash

   rocprofv3 --pmc SQ_WAVES GRBM_COUNT --replay-mode kernel --kernel-replay-beta-enabled -- ./my_app

Each ``--pmc`` group must still fit in one hardware pass; kernel replay does not split a group the
hardware cannot collect together.

Output
======

All counter groups from one kernel-replay run are written together. There is no ``pass_n/``
directory per group, unlike application replay.

JSON
----

Counter records include a ``replay_pass`` field (0-based). All passes of one dispatch share the same
``dispatch_id``; ``replay_pass`` tells them apart. The SDK enforces that identity: one dispatch id is
reserved before the first pass and reused for every pass.

CSV
---

``counter_collection.csv`` uses the same columns as a non-replay run; there is **no**
``Replay_Pass`` column. Passes of a dispatch share ``Dispatch_Id``, and each contributes the
counters from its group, so ``Counter_Name`` identifies the pass when the groups request distinct
counters (as in the example above). When a counter appears in several groups -- a sanity counter
such as ``SQ_WAVES`` in every group -- CSV alone cannot attribute rows to a pass. Use
``--output-format json`` (alone or with ``csv``) in that case.

Default ``rocpd``
-----------------

The default output format is ``rocpd``. Convert it with ``rocpd convert`` as described in
:ref:`using-rocpd-output-format`.

What is snapshotted
===================

Between passes, kernel replay restores coarse-grained device memory (``hipMalloc``) and
module-scope ``__device__`` / ``__constant__`` variables. Unified, managed, ``hipMallocAsync``,
host, fine-grained, kernarg, and executable allocations are not restored.

The snapshot covers **every tracked allocation on the dispatch's GPU**, not only the memory the
kernel can reach, and is a full copy into host RAM. Host memory use and copy time therefore scale
with the GPU's whole tracked footprint: cost is ``O(tracked_bytes × passes)``. The last pass is not
restored, so your application sees the memory its kernel actually produced. See
:ref:`kernel-replay-memory-snapshot`.

Limitations
===========

* **Beta.** The flag, the SDK API, and the output schema may change.
* **Requires** ``--pmc``. Kernel replay is an alternative to application replay for counter groups,
  not a general "run my kernel N times" switch.
* **Each** ``--pmc`` **group must fit one hardware pass.**
* **Counters only.** ``--att``, ``--spm``, and PC sampling are rejected with ``--replay-mode
  kernel``: the CLI has no per-pass toggles, so those services would report each kernel once per
  pass. This is a CLI limitation -- custom tools can place services on specific passes
  (:ref:`using-kernel-replay`).
* **HIP graph launches are not replayed.** A graph launch warns once and runs once (not an error).
* **Only single-packet, single-dispatch submissions** are replayed.
* **Single process.** There is no MPI or cross-process coordination, and no multi-GPU end-to-end CLI
  test. Replaying a kernel that takes part in an inter-process collective is unsafe.
* **Async copies are not fenced.** An ``hsa_amd_memory_async_copy`` on another thread can change
  device memory during the replay window.
* **Stuck drains abort the process** (roughly 60 s).
* **Host RAM duplication** of the tracked device footprint. For large footprints, snapshot plus
  restore can cost more than re-running the application.

When to use kernel replay
=========================

Use it when:

* You need several counter groups on the **same** dispatches.
* Re-running the whole application per group is too slow, or the run is not deterministic, so
  application replay would not compare the same work.
* The kernels of interest write coarse-grained ``hipMalloc`` buffers (and optionally module-scope
  device variables), not unified, managed, or async-pool memory.

Prefer something else when:

* **The kernels form a phase that only makes sense together** -- use range replay
  (:ref:`using-range-replay-rocprofv3`).
* **The tracked device footprint is huge** -- snapshot and restore bandwidth can dominate; use
  application replay or counter-group rotation.
* **The application uses HIP graphs, unified memory, or** ``hipMallocAsync`` for the buffers its
  kernels write.
* **You need only one group**, or can rotate groups across successive dispatches.

See also
========

* :ref:`using-kernel-replay` -- SDK how-to for custom tools
* :ref:`kernel-replay-sdk-api` -- payload and operations
* :ref:`kernel-replay-conceptual` -- how kernel replay works
* :ref:`using-range-replay-rocprofv3` -- replaying a marked phase instead of single dispatches
