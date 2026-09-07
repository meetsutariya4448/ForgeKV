execute_process(
    COMMAND
        "${BENCHMARK}" network --duration 18446744073709551615 --requests 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted a duration outside chrono's range")
endif()
if(NOT error MATCHES "duration is outside supported range")
    message(FATAL_ERROR "benchmark failed for the wrong reason: ${output}${error}")
endif()
