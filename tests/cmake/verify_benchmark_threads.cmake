foreach(mode IN ITEMS contention network)
    if(mode STREQUAL "contention")
        set(extra_arguments --operations-per-thread 1)
    else()
        set(extra_arguments --connections 18446744073709551615)
    endif()
    execute_process(
        COMMAND "${BENCHMARK}" "${mode}" --threads 18446744073709551615 ${extra_arguments}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "${mode} benchmark accepted too many threads")
    endif()
    if(NOT error MATCHES "thread count exceeds barrier participant limit")
        message(FATAL_ERROR "${mode} benchmark reported an unexpected error: ${error}${output}")
    endif()
endforeach()
