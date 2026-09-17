execute_process(
    COMMAND
        "${BENCHMARK}" contention --threads 2
        --operations-per-thread 18446744073709551615
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted an overflowing operation count")
endif()
if(NOT error MATCHES "total contention operation count is not representable")
    message(FATAL_ERROR "benchmark reported an unexpected error: ${error}${output}")
endif()
