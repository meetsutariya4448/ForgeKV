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

execute_process(
    COMMAND
        "${BENCHMARK}" network --key-count 1 --warmup-requests 0
        --requests 18446744073709551615
    RESULT_VARIABLE workload_result
    OUTPUT_VARIABLE workload_output
    ERROR_VARIABLE workload_error
)
if(workload_result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted a measured run that exhausts request ids")
endif()
if(NOT workload_error MATCHES "benchmark workload exhausts request ids")
    message(FATAL_ERROR
        "benchmark reported an unexpected workload error: ${workload_error}${workload_output}")
endif()
