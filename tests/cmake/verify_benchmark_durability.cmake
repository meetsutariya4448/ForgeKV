execute_process(
    COMMAND "${BENCHMARK}" network --durability alwyas --requests 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted an unknown durability label")
endif()
if(NOT error MATCHES "durability must be always, periodic, or none")
    message(FATAL_ERROR "benchmark reported an unexpected error: ${output}${error}")
endif()
