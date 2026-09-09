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
