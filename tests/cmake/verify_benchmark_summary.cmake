set(run_directory "${CMAKE_CURRENT_BINARY_DIR}/benchmark-summary-validation")
file(REMOVE_RECURSE "${run_directory}")
file(MAKE_DIRECTORY "${run_directory}")
file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,valid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE malformed_manifest_result
    OUTPUT_VARIABLE malformed_manifest_output
    ERROR_VARIABLE malformed_manifest_error
)
if(malformed_manifest_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a manifest without its seed column")
endif()
if(NOT malformed_manifest_error MATCHES "manifest is missing required columns: seed")
    message(FATAL_ERROR
        "summary reported an unexpected manifest schema error: "
        "${malformed_manifest_output}${malformed_manifest_error}")
endif()

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
    "\"repetition\":true,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE boolean_metadata_result
    OUTPUT_VARIABLE boolean_metadata_output
    ERROR_VARIABLE boolean_metadata_error
)
if(boolean_metadata_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted boolean trial metadata")
endif()
if(NOT boolean_metadata_error MATCHES "repetition must be an integer")
    message(FATAL_ERROR
        "summary reported an unexpected metadata type error: "
        "${boolean_metadata_output}${boolean_metadata_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,0,7,valid,result\n")
file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":0,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE zero_trial_result
    OUTPUT_VARIABLE zero_trial_output
    ERROR_VARIABLE zero_trial_error
)
if(zero_trial_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted trial zero")
endif()
if(NOT zero_trial_error MATCHES "manifest trial must be a positive integer")
    message(FATAL_ERROR
        "summary reported an unexpected trial bounds error: ${zero_trial_output}${zero_trial_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,01,007,valid,result\n")
file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE noncanonical_identity_result
    OUTPUT_VARIABLE noncanonical_identity_output
    ERROR_VARIABLE noncanonical_identity_error
)
if(noncanonical_identity_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted noncanonical numeric identities")
endif()
if(NOT noncanonical_identity_error MATCHES "canonical decimal form")
    message(FATAL_ERROR
        "summary reported an unexpected numeric identity error: "
        "${noncanonical_identity_output}${noncanonical_identity_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,result\n")

file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"operations_per_second\":11,\"latency_us\":{\"p99\":2},"
    "\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE duplicate_json_result
    OUTPUT_VARIABLE duplicate_json_output
    ERROR_VARIABLE duplicate_json_error
)
if(duplicate_json_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted duplicate JSON fields")
endif()
if(NOT duplicate_json_error MATCHES "duplicate JSON field 'operations_per_second'")
    message(FATAL_ERROR
        "summary reported an unexpected duplicate JSON error: "
        "${duplicate_json_output}${duplicate_json_error}")
endif()

file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":-1,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_metric_result
    OUTPUT_VARIABLE invalid_metric_output
    ERROR_VARIABLE invalid_metric_error
)
if(invalid_metric_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an invalid metric")
endif()
if(NOT invalid_metric_error MATCHES "operations_per_second must be a finite nonnegative number")
    message(FATAL_ERROR
        "summary reported an unexpected metric error: ${invalid_metric_output}${invalid_metric_error}")
endif()

file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
file(APPEND "${run_directory}/manifest.csv"
    "run-1,read-ratio,0.8,1,7,valid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE duplicate_result
    OUTPUT_VARIABLE duplicate_output
    ERROR_VARIABLE duplicate_error
)
if(duplicate_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a duplicate valid trial")
endif()
if(NOT duplicate_error MATCHES "manifest contains duplicate valid trial")
    message(FATAL_ERROR
        "summary reported an unexpected duplicate error: ${duplicate_output}${duplicate_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,result\n"
    "run-1,read-ratio,0.8,1,8,valid,result-2\n")
file(WRITE "${run_directory}/result-2.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":8,\"operations_per_second\":11,"
    "\"latency_us\":{\"p99\":3},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE duplicate_trial_result
    OUTPUT_VARIABLE duplicate_trial_output
    ERROR_VARIABLE duplicate_trial_error
)
if(duplicate_trial_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted one trial with conflicting seeds")
endif()
if(NOT duplicate_trial_error MATCHES "manifest contains duplicate valid trial")
    message(FATAL_ERROR
        "summary reported an unexpected conflicting-seed error: "
        "${duplicate_trial_output}${duplicate_trial_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,result\n"
    "run-2,read-ratio,0.8,2,8,valid,result-2\n")
file(WRITE "${run_directory}/result-2.json"
    "{\"run_id\":\"run-2\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":2,\"seed\":8,\"operations_per_second\":11,"
    "\"latency_us\":{\"p99\":3},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE mixed_run_result
    OUTPUT_VARIABLE mixed_run_output
    ERROR_VARIABLE mixed_run_error
)
if(mixed_run_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted trials from different runs")
endif()
if(NOT mixed_run_error MATCHES "manifest mixes run ids 'run-1' and 'run-2'")
    message(FATAL_ERROR
        "summary reported an unexpected mixed-run error: ${mixed_run_output}${mixed_run_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,../result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unsafe_path_result
    OUTPUT_VARIABLE unsafe_path_output
    ERROR_VARIABLE unsafe_path_error
)
if(unsafe_path_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unsafe result path")
endif()
if(NOT unsafe_path_error MATCHES "output prefix '../result' is not a safe filename")
    message(FATAL_ERROR
        "summary reported an unexpected path error: ${unsafe_path_output}${unsafe_path_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,result\n"
    "run-1,read-ratio,1.0,2,8,vaild,result-2\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unknown_status_result
    OUTPUT_VARIABLE unknown_status_output
    ERROR_VARIABLE unknown_status_error
)
if(unknown_status_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary silently omitted an unknown manifest status")
endif()
if(NOT unknown_status_error MATCHES "manifest contains unknown status 'vaild'")
    message(FATAL_ERROR
        "summary reported an unexpected status error: ${unknown_status_output}${unknown_status_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,   ,0.8,1,7,valid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE blank_identity_result
    OUTPUT_VARIABLE blank_identity_output
    ERROR_VARIABLE blank_identity_error
)
if(blank_identity_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a blank trial identity")
endif()
if(NOT blank_identity_error MATCHES "manifest valid trial has a blank experiment")
    message(FATAL_ERROR
        "summary reported an unexpected blank identity error: "
        "${blank_identity_output}${blank_identity_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,invalid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE empty_result
    OUTPUT_VARIABLE empty_output
    ERROR_VARIABLE empty_error
)
if(empty_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a manifest without valid trials")
endif()
if(NOT empty_error MATCHES "manifest contains no valid trials")
    message(FATAL_ERROR
        "summary reported an unexpected empty-run error: ${empty_output}${empty_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE valid_result
    OUTPUT_VARIABLE valid_output
    ERROR_VARIABLE valid_error
)
if(NOT valid_result EQUAL 0 OR NOT EXISTS "${run_directory}/summary.csv")
    message(FATAL_ERROR "valid benchmark summary failed: ${valid_output}${valid_error}")
endif()
