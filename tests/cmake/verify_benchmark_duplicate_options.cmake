foreach(mode IN ITEMS contention network)
    execute_process(
        COMMAND "${BENCHMARK}" "${mode}" --threads 1 --threads 2
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "${mode} benchmark accepted a duplicate option")
    endif()
    if(NOT error MATCHES "duplicate ${mode} option")
        message(FATAL_ERROR "${mode} benchmark reported an unexpected error: ${output}${error}")
    endif()
endforeach()
