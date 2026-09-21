execute_process(
    COMMAND "${SERVER}" --workers 1 --workers 2
    RESULT_VARIABLE value_result
    OUTPUT_VARIABLE value_output
    ERROR_VARIABLE value_error
)
if(value_result EQUAL 0)
    message(FATAL_ERROR "server accepted a duplicate valued option")
endif()
if(NOT value_error MATCHES "duplicate server option: --workers")
    message(FATAL_ERROR "server reported an unexpected error: ${value_output}${value_error}")
endif()

execute_process(
    COMMAND "${SERVER}" --no-background-compaction --no-background-compaction
    RESULT_VARIABLE flag_result
    OUTPUT_VARIABLE flag_output
    ERROR_VARIABLE flag_error
)
if(flag_result EQUAL 0)
    message(FATAL_ERROR "server accepted a duplicate flag")
endif()
if(NOT flag_error MATCHES "duplicate server option: --no-background-compaction")
    message(FATAL_ERROR "server reported an unexpected error: ${flag_output}${flag_error}")
endif()
