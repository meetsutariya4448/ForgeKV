foreach(shards IN ITEMS "" ",4" "4,,16" "4,")
    execute_process(
        COMMAND "${BENCHMARK}" contention --shards "${shards}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )

    if(result EQUAL 0)
        message(FATAL_ERROR "benchmark accepted malformed shard list '${shards}'")
    endif()
    if(NOT error MATCHES "shard list must not")
        message(FATAL_ERROR
            "benchmark failed for the wrong reason with '${shards}': ${output}${error}"
        )
    endif()
endforeach()

execute_process(
    COMMAND "${BENCHMARK}" contention --shards "4,16,4"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "benchmark accepted duplicate shard counts")
endif()
if(NOT error MATCHES "shard list must not contain duplicates")
    message(FATAL_ERROR "benchmark reported an unexpected duplicate error: ${output}${error}")
endif()
