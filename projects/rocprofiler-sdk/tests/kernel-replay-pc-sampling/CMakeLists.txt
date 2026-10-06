#
# Kernel replay with PC sampling: the passes a tool samples, and only those, contribute samples.
# Like the other PC sampling tests these run only where the job enables PC sampling
# (ROCPROFILER_PC_SAMPLING_BETA_ENABLED) and skip otherwise.
#
cmake_minimum_required(VERSION 3.21.0 FATAL_ERROR)

if(NOT CMAKE_HIP_COMPILER)
    find_program(
        amdclangpp_EXECUTABLE
        NAMES amdclang++
        HINTS ${ROCM_PATH} ENV ROCM_PATH /opt/rocm
        PATHS ${ROCM_PATH} ENV ROCM_PATH /opt/rocm
        PATH_SUFFIXES bin llvm/bin NO_CACHE)
    mark_as_advanced(amdclangpp_EXECUTABLE)

    if(amdclangpp_EXECUTABLE)
        set(CMAKE_HIP_COMPILER "${amdclangpp_EXECUTABLE}")
    endif()
endif()

project(rocprofiler-sdk-tests-kernel-replay-pc-sampling LANGUAGES CXX HIP)

foreach(_TYPE DEBUG MINSIZEREL RELEASE RELWITHDEBINFO)
    if("${CMAKE_HIP_FLAGS_${_TYPE}}" STREQUAL "")
        set(CMAKE_HIP_FLAGS_${_TYPE} "${CMAKE_CXX_FLAGS_${_TYPE}}")
    endif()
endforeach()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_HIP_STANDARD 17)
set(CMAKE_HIP_EXTENSIONS OFF)
set(CMAKE_HIP_STANDARD_REQUIRED ON)

find_package(rocprofiler-sdk REQUIRED)

add_library(kernel-replay-pc-sampling-client SHARED)
target_sources(kernel-replay-pc-sampling-client PRIVATE client.cpp)
target_link_libraries(
    kernel-replay-pc-sampling-client PRIVATE rocprofiler-sdk::rocprofiler-sdk
                                             rocprofiler-sdk::tests-build-flags)

set_source_files_properties(main.cpp PROPERTIES LANGUAGE HIP)
add_executable(kernel-replay-pc-sampling-testapp)
target_sources(kernel-replay-pc-sampling-testapp PRIVATE main.cpp)
target_link_libraries(kernel-replay-pc-sampling-testapp
                      PRIVATE rocprofiler-sdk::tests-build-flags)

if(ROCPROFILER_MEMCHECK_PRELOAD_ENV_VALUE)
    set(PRELOAD_ENV
        "${ROCPROFILER_MEMCHECK_PRELOAD_ENV_VALUE}:$<TARGET_FILE:kernel-replay-pc-sampling-client>"
        )
else()
    set(PRELOAD_ENV "$<TARGET_FILE:kernel-replay-pc-sampling-client>")
endif()

include(rocprofiler-sdk-utilities)
rocprofiler_sdk_pc_sampling_disabled(IS_PC_SAMPLING_DISABLED)

function(add_kr_pcs_test NAME)
    cmake_parse_arguments(arg "" "" "ENVIRONMENT" ${ARGN})
    set(_disabled)
    if(IS_PC_SAMPLING_DISABLED)
        set(_disabled DISABLED ${IS_PC_SAMPLING_DISABLED})
    endif()
    rocprofiler_add_integration_execute_test(
        ${NAME}
        COMMAND $<TARGET_FILE:kernel-replay-pc-sampling-testapp>
        DEPENDS kernel-replay-pc-sampling-client
        TIMEOUT 180
        LABELS "integration-tests;kernel-replay;pc-sampling"
        PRELOAD "${PRELOAD_ENV}"
        ENVIRONMENT ${arg_ENVIRONMENT} ${_disabled}
        PASS_REGULAR_EXPRESSION "\\[kr-pcs\\] PASS"
        FAIL_REGULAR_EXPRESSION
            "\\[kr-pcs\\] FAIL|\\[app\\] FAIL|${ROCPROFILER_DEFAULT_FAIL_REGEX}"
        SKIP_REGULAR_EXPRESSION "PC sampling unavailable")
endfunction()

# Only the chosen pass samples: the replayed dispatch collects one execution's worth.
add_kr_pcs_test(test-kernel-replay-pc-sampling-first-pass ENVIRONMENT
                "KR_PCS_PASSES=4;KR_PCS_PASS=0;KR_PCS_EXPECT=one")
add_kr_pcs_test(test-kernel-replay-pc-sampling-last-pass ENVIRONMENT
                "KR_PCS_PASSES=4;KR_PCS_PASS=3;KR_PCS_EXPECT=one")

# No local toggles: every pass samples, and the replay blits between them still do not.
add_kr_pcs_test(test-kernel-replay-pc-sampling-every-pass ENVIRONMENT
                "KR_PCS_PASSES=4;KR_PCS_PASS=-1;KR_PCS_EXPECT=all")
