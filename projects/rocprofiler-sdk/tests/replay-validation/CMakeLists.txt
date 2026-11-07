#
# Correctness check for the replay integration preview: one kernel replay run and one range replay
# run, each checked against the result the application reads back after replay.
#
cmake_minimum_required(VERSION 3.21.0 FATAL_ERROR)

project(rocprofiler-sdk-tests-replay-validation LANGUAGES C CXX)

find_package(Python3 REQUIRED)

add_test(
    NAME test-replay-validation
    COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/validate.py --kr-testapp
        $<TARGET_FILE:kernel-replay-perf-testapp> --kr-client
        $<TARGET_FILE:kernel-replay-perf-client> --rr-testapp
        $<TARGET_FILE:range-replay-perf-testapp> --rr-client
        $<TARGET_FILE:range-replay-perf-client> --passes 4)

set_tests_properties(
    test-replay-validation
    PROPERTIES TIMEOUT
               300
               LABELS
               "integration-tests;kernel-replay;range-replay;replay-validation"
               RESOURCE_LOCK
               rocprofiler_replay_validation
               DEPENDS
               "kernel-replay-perf-client;kernel-replay-perf-testapp;range-replay-perf-client;range-replay-perf-testapp"
               PASS_REGULAR_EXPRESSION
               "\\[replay-validate\\] PASS"
               FAIL_REGULAR_EXPRESSION
               "${ROCPROFILER_DEFAULT_FAIL_REGEX}")
