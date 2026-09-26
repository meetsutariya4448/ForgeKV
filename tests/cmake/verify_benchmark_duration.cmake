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

execute_process(
    COMMAND "${BENCHMARK}" network --duration 9223372037 --requests 1
    RESULT_VARIABLE clock_result
    OUTPUT_VARIABLE clock_output
    ERROR_VARIABLE clock_error
)

if(clock_result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted a duration outside the steady clock range")
endif()
if(NOT clock_error MATCHES "duration exceeds steady clock range")
    message(FATAL_ERROR
        "benchmark reported an unexpected steady-clock error: ${clock_output}${clock_error}")
endif()
