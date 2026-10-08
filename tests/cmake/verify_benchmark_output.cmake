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

set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/benchmark-output-guard")
set(output_prefix "${output_directory}/trial")
file(REMOVE_RECURSE "${output_directory}")
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${output_prefix}.json" "preserve-me")
execute_process(
    COMMAND "${BENCHMARK}" network --output-prefix "${output_prefix}" --requests 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "benchmark overwrote an existing result")
endif()
if(NOT error MATCHES "refusing to overwrite benchmark output")
    message(FATAL_ERROR "benchmark reported an unexpected overwrite error: ${output}${error}")
endif()
file(READ "${output_prefix}.json" contents)
if(NOT contents STREQUAL "preserve-me")
    message(FATAL_ERROR "benchmark modified existing output before refusing it")
endif()
file(REMOVE_RECURSE "${output_directory}")
