if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "SOURCE_DIR and OUTPUT_FILE are required")
endif()

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE git_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE git_sha_result
)
if(NOT git_sha_result EQUAL 0 OR NOT git_sha MATCHES "^[0-9a-fA-F]+$")
    set(git_sha unknown)
endif()

execute_process(
    COMMAND git status --porcelain
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE git_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE git_status_result
)
if(NOT git_status_result EQUAL 0 OR git_status)
    set(git_dirty 1)
else()
    set(git_dirty 0)
endif()

string(CONCAT contents
    "#pragma once\n\n"
    "#define FORGEKV_GIT_SHA \"${git_sha}\"\n"
    "#define FORGEKV_GIT_DIRTY ${git_dirty}\n")
get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
set(temporary_file "${OUTPUT_FILE}.tmp")
file(WRITE "${temporary_file}" "${contents}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${temporary_file}" "${OUTPUT_FILE}")
file(REMOVE "${temporary_file}")
