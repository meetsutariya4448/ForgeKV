execute_process(
    COMMAND
        "${BENCHMARK}" contention
        --threads 2
        --operations-per-thread 10
        --keys 2
        --repetitions 1
        --shards 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 5
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "small contention benchmark did not complete: ${result}: ${error}")
endif()

if(NOT output MATCHES "same-key,1,2,2,20,1")
    message(FATAL_ERROR "contention output omitted the same-key result: ${output}")
endif()

if(NOT output MATCHES "distributed-key,1,2,2,20,1")
    message(FATAL_ERROR "contention output omitted the distributed-key result: ${output}")
endif()
