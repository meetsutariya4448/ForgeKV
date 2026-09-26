set(run_directory "${CMAKE_CURRENT_BINARY_DIR}/benchmark-summary-validation")
file(REMOVE_RECURSE "${run_directory}")
file(MAKE_DIRECTORY "${run_directory}")
file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,result\n")
file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":2,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")

execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE mismatch_result
    OUTPUT_VARIABLE mismatch_output
    ERROR_VARIABLE mismatch_error
)
if(mismatch_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted mismatched trial metadata")
endif()
if(NOT mismatch_error MATCHES "repetition=2 does not match manifest value 1")
    message(FATAL_ERROR
        "summary reported an unexpected mismatch error: ${mismatch_output}${mismatch_error}")
endif()

file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE valid_result
    OUTPUT_VARIABLE valid_output
    ERROR_VARIABLE valid_error
)
if(NOT valid_result EQUAL 0 OR NOT EXISTS "${run_directory}/summary.csv")
    message(FATAL_ERROR "valid benchmark summary failed: ${valid_output}${valid_error}")
endif()
