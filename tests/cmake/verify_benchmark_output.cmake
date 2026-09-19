execute_process(
    COMMAND "${BENCHMARK}" network --output-prefix "" --requests 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted an explicitly empty output prefix")
endif()
if(NOT error MATCHES "output prefix must not be empty")
    message(FATAL_ERROR "benchmark reported an unexpected error: ${output}${error}")
endif()
