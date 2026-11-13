.. meta::
    :description: "ROCprofiler-SDK is a tooling infrastructure for profiling general-purpose GPU compute applications running on the ROCm software."
    :keywords: "ROCprofiler-SDK, ROCProfiler-SDK samples"

.. _rocprofiler-sdk-samples:

ROCprofiler-SDK samples
========================

The samples are provided to help you see the profiler in action.

Finding samples
---------------

The ROCm installation provides sample programs and ``rocprofv3`` tool.

- Sample programs are installed here:

.. code-block:: bash

    /opt/rocm/share/rocprofiler-sdk/samples

- ``rocprofv3`` tool is installed here:

.. code-block:: bash

    /opt/rocm/bin

Building samples
-----------------

To build samples from any directory, run:

.. code-block:: bash

    cmake -B build-rocprofiler-sdk-samples /opt/rocm/share/rocprofiler-sdk/samples -DCMAKE_PREFIX_PATH=/opt/rocm
    cmake --build build-rocprofiler-sdk-samples --target all --parallel 8


Running samples
---------------

To run the built samples, ``cd`` into the ``build-rocprofiler-sdk-samples`` directory and run:

.. code-block:: bash

    ctest -V

The ``-V`` option enables verbose output, providing detailed information about the test execution.

Replay samples
--------------

The kernel replay and range replay samples are small custom tools preloaded onto a minimal HIP
application. Run just one family with a test-name filter:

.. code-block:: bash

    ctest -R '^kernel-replay-' --output-on-failure
    ctest -R '^range-replay-' --output-on-failure

Each family's ``README.md`` (``samples/kernel_replay/`` and ``samples/range_replay/``) explains
what every sample demonstrates. The matching guides are :ref:`using-kernel-replay` and
:ref:`using-range-replay`.
