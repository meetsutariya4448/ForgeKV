execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_BENCH_PORT=70000"
        /bin/sh "${MATRIX_RUNNER}" quick
    RESULT_VARIABLE port_result
    OUTPUT_VARIABLE port_output
    ERROR_VARIABLE port_error
)
if(port_result EQUAL 0)
    message(FATAL_ERROR "benchmark matrix accepted an invalid TCP port")
endif()
if(NOT port_error MATCHES "FORGEKV_BENCH_PORT must be an integer from 1 to 65535")
    message(FATAL_ERROR "matrix reported an unexpected port error: ${port_output}${port_error}")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_BENCH_SEED=not-a-number"
        /bin/sh "${MATRIX_RUNNER}" quick
    RESULT_VARIABLE seed_result
    OUTPUT_VARIABLE seed_output
    ERROR_VARIABLE seed_error
)
if(seed_result EQUAL 0)
    message(FATAL_ERROR "benchmark matrix accepted an invalid seed")
endif()
if(NOT seed_error MATCHES "FORGEKV_BENCH_SEED must be a nonnegative integer")
    message(FATAL_ERROR "matrix reported an unexpected seed error: ${seed_output}${seed_error}")
endif()
