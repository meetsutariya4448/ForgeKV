execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_BENCH_THREADS=04"
        /bin/sh "${M3_RUNNER}"
    RESULT_VARIABLE threads_result
    OUTPUT_VARIABLE threads_output
    ERROR_VARIABLE threads_error
)
if(threads_result EQUAL 0)
    message(FATAL_ERROR "M3 contention runner accepted a noncanonical thread count")
endif()

foreach(shards IN ITEMS "1,04,16" "1,4,4")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "FORGEKV_BENCH_SHARDS=${shards}"
            /bin/sh "${M3_RUNNER}"
        RESULT_VARIABLE shards_result
        OUTPUT_VARIABLE shards_output
        ERROR_VARIABLE shards_error
    )
    if(shards_result EQUAL 0)
        message(FATAL_ERROR "M3 contention runner accepted malformed shards: ${shards}")
    endif()
    if(NOT shards_error MATCHES "FORGEKV_BENCH_SHARDS must")
        message(FATAL_ERROR
            "M3 contention runner reported an unexpected shard-list error: "
            "${shards_output}${shards_error}")
    endif()
endforeach()
if(NOT threads_error MATCHES "FORGEKV_BENCH_THREADS must be a canonical positive integer")
    message(FATAL_ERROR
        "M3 contention runner reported an unexpected thread-count error: "
        "${threads_output}${threads_error}")
endif()

set(debug_build "${CMAKE_CURRENT_BINARY_DIR}/m3-contention-debug-build")
file(REMOVE_RECURSE "${debug_build}")
file(MAKE_DIRECTORY "${debug_build}")
file(WRITE "${debug_build}/forgekv-bench" "#!/bin/sh\nexit 0\n")
execute_process(COMMAND chmod +x "${debug_build}/forgekv-bench")
file(WRITE "${debug_build}/CMakeCache.txt" "CMAKE_BUILD_TYPE:STRING=Debug\n")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_BENCH_BUILD_DIR=${debug_build}"
        /bin/sh "${M3_RUNNER}"
    RESULT_VARIABLE build_type_result
    OUTPUT_VARIABLE build_type_output
    ERROR_VARIABLE build_type_error
)
if(build_type_result EQUAL 0)
    message(FATAL_ERROR "M3 contention runner accepted a Debug build")
endif()
if(NOT build_type_error MATCHES "requires a Release build; found Debug")
    message(FATAL_ERROR
        "M3 contention runner reported an unexpected build-type error: "
        "${build_type_output}${build_type_error}")
endif()
