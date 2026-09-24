execute_process(
    COMMAND
        "${BENCHMARK}" network --skip-preload --warmup-requests 1 --requests 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted warmup work while preload was disabled")
endif()
if(NOT error MATCHES "warmup requests require dataset preload")
    message(FATAL_ERROR "benchmark reported an unexpected error: ${output}${error}")
endif()
