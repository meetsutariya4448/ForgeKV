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

set(profile_test_root "${CMAKE_CURRENT_BINARY_DIR}/profile-output-reservation")
set(fake_bin "${profile_test_root}/bin")
set(fake_build "${profile_test_root}/build")
set(output_dir "${profile_test_root}/evidence")
file(REMOVE_RECURSE "${profile_test_root}")
file(MAKE_DIRECTORY "${fake_bin}" "${fake_build}" "${output_dir}")
file(WRITE "${fake_bin}/uname" "#!/bin/sh\necho Linux\n")
execute_process(COMMAND chmod +x "${fake_bin}/uname")
foreach(binary forgekv-server forgekv-bench forgekv-cli)
    file(WRITE "${fake_build}/${binary}" "#!/bin/sh\nexit 0\n")
    execute_process(COMMAND chmod +x "${fake_build}/${binary}")
endforeach()
file(WRITE "${fake_build}/CMakeCache.txt" "CMAKE_BUILD_TYPE:STRING=Release\n")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=${fake_bin}:$ENV{PATH}"
        "FORGEKV_PROFILE_BUILD_DIR=${fake_build}"
        "FORGEKV_PROFILE_OUTPUT_DIR=${output_dir}"
        /bin/sh "${PROFILE_RUNNER}"
    RESULT_VARIABLE output_result
    OUTPUT_VARIABLE output_output
    ERROR_VARIABLE output_error
)
if(output_result EQUAL 0)
    message(FATAL_ERROR "profile runner reused an existing output directory")
endif()
if(NOT output_error MATCHES "refusing to overwrite profiling output")
    message(FATAL_ERROR
        "profile runner reported an unexpected output error: ${output_output}${output_error}")
endif()
