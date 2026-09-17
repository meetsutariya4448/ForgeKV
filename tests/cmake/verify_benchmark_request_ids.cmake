execute_process(
    COMMAND
        "${BENCHMARK}" network --key-count 18446744073709551615
        --warmup-requests 1 --requests 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted a preload that exhausts request ids")
endif()
if(NOT error MATCHES "preload and warmup exhaust request ids")
    message(FATAL_ERROR "benchmark reported an unexpected error: ${error}${output}")
endif()
