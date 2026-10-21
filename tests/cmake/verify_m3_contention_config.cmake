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

set(missing_metadata_build "${CMAKE_CURRENT_BINARY_DIR}/m3-contention-missing-metadata-build")
file(REMOVE_RECURSE "${missing_metadata_build}")
file(MAKE_DIRECTORY "${missing_metadata_build}")
file(WRITE "${missing_metadata_build}/forgekv-bench" "#!/bin/sh\nexit 0\n")
execute_process(COMMAND chmod +x "${missing_metadata_build}/forgekv-bench")
file(WRITE "${missing_metadata_build}/CMakeCache.txt" "CMAKE_BUILD_TYPE:STRING=Release\n")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_BENCH_BUILD_DIR=${missing_metadata_build}"
        /bin/sh "${M3_RUNNER}"
    RESULT_VARIABLE missing_metadata_result
    OUTPUT_VARIABLE missing_metadata_output
    ERROR_VARIABLE missing_metadata_error
)
if(missing_metadata_result EQUAL 0)
    message(FATAL_ERROR "M3 contention runner accepted missing compilation metadata")
endif()
if(NOT missing_metadata_error MATCHES "requires compile_commands.json")
    message(FATAL_ERROR
        "M3 contention runner reported an unexpected metadata error: "
        "${missing_metadata_output}${missing_metadata_error}")
endif()

set(failing_build "${CMAKE_CURRENT_BINARY_DIR}/m3-contention-failing-build")
set(failing_output "${CMAKE_CURRENT_BINARY_DIR}/m3-contention-failing-output")
file(REMOVE_RECURSE "${failing_build}" "${failing_output}")
file(MAKE_DIRECTORY "${failing_build}" "${failing_output}")
file(WRITE "${failing_build}/forgekv-bench" "#!/bin/sh\nexit 7\n")
execute_process(COMMAND chmod +x "${failing_build}/forgekv-bench")
file(WRITE "${failing_build}/fake-cxx" "#!/bin/sh\nprintf 'fake c++ 1.0\\n'\n")
execute_process(COMMAND chmod +x "${failing_build}/fake-cxx")
file(WRITE "${failing_build}/CMakeCache.txt"
    "CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_CXX_COMPILER:FILEPATH=${failing_build}/fake-cxx\n")
file(WRITE "${failing_build}/compile_commands.json"
    "[{\"file\":\"apps/forgekv-bench/main.cpp\",\"command\":\"c++ main.cpp\"}]\n")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "FORGEKV_BENCH_BUILD_DIR=${failing_build}"
        "FORGEKV_BENCH_OUTPUT_DIR=${failing_output}"
        /bin/sh "${M3_RUNNER}"
    RESULT_VARIABLE failing_result
    OUTPUT_VARIABLE failing_output_text
    ERROR_VARIABLE failing_error
)
if(failing_result EQUAL 0)
    message(FATAL_ERROR "M3 contention runner ignored a benchmark failure")
endif()
file(GLOB failed_evidence "${failing_output}/*" "${failing_output}/.*")
list(FILTER failed_evidence EXCLUDE REGEX "/\\.\\.?$")
if(failed_evidence)
    message(FATAL_ERROR
        "M3 contention runner left partial evidence after failure: ${failed_evidence}")
endif()

set(success_build "${CMAKE_CURRENT_BINARY_DIR}/m3-contention-success-build")
set(success_output "${CMAKE_CURRENT_BINARY_DIR}/m3-contention-success-output")
set(success_bin "${CMAKE_CURRENT_BINARY_DIR}/m3-contention-success-bin")
file(REMOVE_RECURSE "${success_build}" "${success_output}" "${success_bin}")
file(MAKE_DIRECTORY "${success_build}" "${success_output}" "${success_bin}")
file(WRITE "${success_build}/forgekv-bench"
    "#!/bin/sh\nprintf 'workload,shards,threads,operations,seconds,ops_per_second\\n'\n")
execute_process(COMMAND chmod +x "${success_build}/forgekv-bench")
file(WRITE "${success_build}/fake-cxx" "#!/bin/sh\nprintf 'fake c++ 1.0\\n'\n")
execute_process(COMMAND chmod +x "${success_build}/fake-cxx")
file(WRITE "${success_build}/CMakeCache.txt"
    "CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_CXX_COMPILER:FILEPATH=${success_build}/fake-cxx\n")
file(WRITE "${success_build}/compile_commands.json"
    "[{\"file\":\"apps/forgekv-bench/main.cpp\",\"command\":\"c++ main.cpp\"}]\n")
file(WRITE "${success_bin}/git" [=[#!/bin/sh
case "$*" in
  *"rev-parse HEAD"*) printf '0123456789abcdef\n' ;;
  *"status --porcelain"*) printf ' M src/benchmark.cpp\n' ;;
  *) exit 1 ;;
esac
]=])
execute_process(COMMAND chmod +x "${success_bin}/git")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=${success_bin}:$ENV{PATH}"
        "FORGEKV_BENCH_BUILD_DIR=${success_build}"
        "FORGEKV_BENCH_OUTPUT_DIR=${success_output}"
        /bin/sh "${M3_RUNNER}"
    RESULT_VARIABLE success_result
    OUTPUT_VARIABLE success_output_text
    ERROR_VARIABLE success_error
)
if(NOT success_result EQUAL 0)
    message(FATAL_ERROR
        "valid M3 contention run failed: ${success_output_text}${success_error}")
endif()
file(GLOB success_evidence "${success_output}/*.csv")
list(LENGTH success_evidence success_evidence_count)
if(NOT success_evidence_count EQUAL 1)
    message(FATAL_ERROR "M3 contention runner did not publish exactly one result")
endif()
list(GET success_evidence 0 success_evidence_path)
file(READ "${success_evidence_path}" success_evidence_text)
if(NOT success_evidence_text MATCHES "# working_tree_dirty,true")
    message(FATAL_ERROR "M3 contention evidence omitted dirty-tree provenance")
endif()
if(NOT success_evidence_text MATCHES "# compiler,fake c\\+\\+ 1.0")
    message(FATAL_ERROR "M3 contention evidence did not identify the configured compiler")
endif()
