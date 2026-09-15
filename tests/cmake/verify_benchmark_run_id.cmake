execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_BENCH_BUILD_DIR=${BUILD_DIR}"
        "FORGEKV_BENCH_RUN_ID=../outside-evidence"
        /bin/sh "${MATRIX_RUNNER}" quick
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "benchmark matrix accepted a path-traversing run id")
endif()
if(NOT error MATCHES "safe filename component")
    message(FATAL_ERROR "benchmark matrix failed for the wrong reason: ${output}${error}")
endif()
