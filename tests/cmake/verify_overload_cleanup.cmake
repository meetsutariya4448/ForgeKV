set(test_directory "${CMAKE_CURRENT_BINARY_DIR}/overload-cleanup-validation")
file(REMOVE_RECURSE "${test_directory}")
file(MAKE_DIRECTORY "${test_directory}")
set(fake_server "${test_directory}/invalid-server")
file(WRITE "${fake_server}" "not an executable format\n")
file(CHMOD "${fake_server}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

execute_process(
    COMMAND
        ${CMAKE_COMMAND} -E env "TMPDIR=${test_directory}"
        python3 "${OVERLOAD_SCRIPT}" --server "${fake_server}"
        --output "${test_directory}/result.json"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(result EQUAL 0)
    message(FATAL_ERROR "overload scenario unexpectedly launched an invalid executable")
endif()

file(GLOB leaked_directories "${test_directory}/forgekv-overload-*")
if(leaked_directories)
    message(FATAL_ERROR "overload scenario leaked temporary data: ${leaked_directories}")
endif()
