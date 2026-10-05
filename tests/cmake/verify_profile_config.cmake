execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_PROFILE_RUN_ID=../outside-evidence"
        /bin/sh "${PROFILE_RUNNER}"
    RESULT_VARIABLE run_id_result
    OUTPUT_VARIABLE run_id_output
    ERROR_VARIABLE run_id_error
)
if(run_id_result EQUAL 0)
    message(FATAL_ERROR "profile runner accepted a path-traversing run id")
endif()
if(NOT run_id_error MATCHES "FORGEKV_PROFILE_RUN_ID must be a safe filename component")
    message(FATAL_ERROR
        "profile runner reported an unexpected run-id error: ${run_id_output}${run_id_error}")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_PROFILE_PORT=70000"
        /bin/sh "${PROFILE_RUNNER}"
    RESULT_VARIABLE port_result
    OUTPUT_VARIABLE port_output
    ERROR_VARIABLE port_error
)
if(port_result EQUAL 0)
    message(FATAL_ERROR "profile runner accepted an invalid TCP port")
endif()
if(NOT port_error MATCHES "FORGEKV_PROFILE_PORT must be an integer from 1 to 65535")
    message(FATAL_ERROR
        "profile runner reported an unexpected port error: ${port_output}${port_error}")
endif()
