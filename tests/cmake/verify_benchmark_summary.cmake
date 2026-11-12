set(run_directory "${CMAKE_CURRENT_BINARY_DIR}/benchmark-summary-validation")
file(REMOVE_RECURSE "${run_directory}")
file(MAKE_DIRECTORY "${run_directory}")
set(external_manifest "${CMAKE_CURRENT_BINARY_DIR}/benchmark-summary-external-manifest.csv")
file(WRITE "${external_manifest}"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n")
file(CREATE_LINK "${external_manifest}" "${run_directory}/manifest.csv" SYMBOLIC)
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE manifest_symlink_result
    OUTPUT_VARIABLE manifest_symlink_output
    ERROR_VARIABLE manifest_symlink_error
)
if(manifest_symlink_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary followed a manifest symlink")
endif()
if(NOT manifest_symlink_error MATCHES "manifest must not be a symbolic link")
    message(FATAL_ERROR
        "summary reported an unexpected manifest symlink error: "
        "${manifest_symlink_output}${manifest_symlink_error}")
endif()
file(REMOVE "${run_directory}/manifest.csv")

string(REPEAT "x" 1048577 oversized_manifest)
file(WRITE "${run_directory}/manifest.csv" "${oversized_manifest}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE oversized_manifest_result
    OUTPUT_VARIABLE oversized_manifest_output
    ERROR_VARIABLE oversized_manifest_error
)
if(oversized_manifest_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an oversized manifest")
endif()
if(NOT oversized_manifest_error MATCHES "manifest exceeds the evidence size limit")
    message(FATAL_ERROR
        "summary reported an unexpected manifest size error: "
        "${oversized_manifest_output}${oversized_manifest_error}")
endif()

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
    "../run-1,read-ratio,0.8,1,7,valid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unsafe_run_id_result
    OUTPUT_VARIABLE unsafe_run_id_output
    ERROR_VARIABLE unsafe_run_id_error
)
if(unsafe_run_id_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unsafe run identifier")
endif()
if(NOT unsafe_run_id_error MATCHES "manifest run_id '../run-1' is not a safe identifier")
    message(FATAL_ERROR
        "summary reported an unexpected run identifier error: "
        "${unsafe_run_id_output}${unsafe_run_id_error}")
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
    "\"repetition\":1,\"seed\":7,\"operations\":100,\"seconds\":2,"
    "\"operations_per_second\":99,\"latency_us\":{\"p99\":2},"
    "\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE inconsistent_throughput_result
    OUTPUT_VARIABLE inconsistent_throughput_output
    ERROR_VARIABLE inconsistent_throughput_error
)
if(inconsistent_throughput_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted inconsistent throughput evidence")
endif()
if(NOT inconsistent_throughput_error MATCHES
   "throughput is inconsistent with operations and seconds")
    message(FATAL_ERROR
        "summary reported an unexpected throughput error: "
        "${inconsistent_throughput_output}${inconsistent_throughput_error}")
endif()

file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p50\":5,\"p95\":4,\"p99\":6,\"max\":7},"
    "\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unordered_latency_result
    OUTPUT_VARIABLE unordered_latency_output
    ERROR_VARIABLE unordered_latency_error
)
if(unordered_latency_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted unordered latency percentiles")
endif()
if(NOT unordered_latency_error MATCHES "latency percentiles are not ordered")
    message(FATAL_ERROR
        "summary reported an unexpected latency error: "
        "${unordered_latency_output}${unordered_latency_error}")
endif()

file(WRITE "${run_directory}/result.json"
    "{\"timestamp_utc\":\"2026-10-26T23:59:59Z\",\"git_sha\":\"main\","
    "\"working_tree_dirty\":false,\"compiler\":\"Clang 17\",\"build\":\"Release\","
    "\"system\":\"test system\",\"hardware_threads\":8,"
    "\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_provenance_result
    OUTPUT_VARIABLE invalid_provenance_output
    ERROR_VARIABLE invalid_provenance_error
)
if(invalid_provenance_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted invalid build provenance")
endif()
if(NOT invalid_provenance_error MATCHES "git_sha must be a 40-character commit")
    message(FATAL_ERROR
        "summary reported an unexpected provenance error: "
        "${invalid_provenance_output}${invalid_provenance_error}")
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
if(NOT duplicate_error MATCHES "manifest contains duplicate trial")
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
if(NOT duplicate_trial_error MATCHES "manifest contains duplicate trial")
    message(FATAL_ERROR
        "summary reported an unexpected conflicting-seed error: "
        "${duplicate_trial_output}${duplicate_trial_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,valid,result\n"
    "run-1,read-ratio,0.8,1,7,invalid,result-retry\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE duplicate_failed_result
    OUTPUT_VARIABLE duplicate_failed_output
    ERROR_VARIABLE duplicate_failed_error
)
if(duplicate_failed_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a duplicate failed trial")
endif()
if(NOT duplicate_failed_error MATCHES "manifest contains duplicate trial")
    message(FATAL_ERROR
        "summary reported an unexpected duplicate failed-trial error: "
        "${duplicate_failed_output}${duplicate_failed_error}")
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
    "run-1,read-ratio,0.8,1,7,valid,result\n"
    "run-2,read-ratio,1.0,2,8,invalid,result-2\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE mixed_invalid_run_result
    OUTPUT_VARIABLE mixed_invalid_run_output
    ERROR_VARIABLE mixed_invalid_run_error
)
if(mixed_invalid_run_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary ignored a foreign run id on an invalid trial")
endif()
if(NOT mixed_invalid_run_error MATCHES "manifest mixes run ids 'run-1' and 'run-2'")
    message(FATAL_ERROR
        "summary reported an unexpected invalid-trial run error: "
        "${mixed_invalid_run_output}${mixed_invalid_run_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,01,007,invalid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_identity_result
    OUTPUT_VARIABLE invalid_identity_output
    ERROR_VARIABLE invalid_identity_error
)
if(invalid_identity_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted noncanonical invalid-trial identities")
endif()
if(NOT invalid_identity_error MATCHES "canonical decimal form")
    message(FATAL_ERROR
        "summary reported an unexpected invalid-trial identity error: "
        "${invalid_identity_output}${invalid_identity_error}")
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
if(NOT blank_identity_error MATCHES "manifest trial has an invalid experiment")
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
set(external_result "${CMAKE_CURRENT_BINARY_DIR}/benchmark-summary-external-result.json")
file(WRITE "${external_result}"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
file(REMOVE "${run_directory}/result.json")
file(CREATE_LINK "${external_result}" "${run_directory}/result.json" SYMBOLIC)
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE symlink_result
    OUTPUT_VARIABLE symlink_output
    ERROR_VARIABLE symlink_error
)
if(symlink_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary followed a result symlink")
endif()
if(NOT symlink_error MATCHES "result.json must not be a symbolic link")
    message(FATAL_ERROR
        "summary reported an unexpected result symlink error: ${symlink_output}${symlink_error}")
endif()

file(REMOVE "${run_directory}/result.json")
file(WRITE "${run_directory}/result.json"
    "{\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations_per_second\":10,"
    "\"latency_us\":{\"p99\":2},\"errors\":0,\"connection_errors\":0}\n")
file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,,0.8,1,7,invalid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_identity_text_result
    OUTPUT_VARIABLE invalid_identity_text_output
    ERROR_VARIABLE invalid_identity_text_error
)
if(invalid_identity_text_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted blank invalid-trial metadata")
endif()
if(NOT invalid_identity_text_error MATCHES "manifest trial has an invalid experiment")
    message(FATAL_ERROR
        "summary reported an unexpected invalid-trial metadata error: "
        "${invalid_identity_text_output}${invalid_identity_text_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,invalid,../result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_prefix_result
    OUTPUT_VARIABLE invalid_prefix_output
    ERROR_VARIABLE invalid_prefix_error
)
if(invalid_prefix_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unsafe invalid-trial output prefix")
endif()
if(NOT invalid_prefix_error MATCHES "output prefix '../result' is not a safe filename")
    message(FATAL_ERROR
        "summary reported an unexpected invalid-trial prefix error: "
        "${invalid_prefix_output}${invalid_prefix_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,1,read-ratio,0.8,1,7,invalid,server-not-ready,result,../resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_resource_name_result
    OUTPUT_VARIABLE invalid_resource_name_output
    ERROR_VARIABLE invalid_resource_name_error
)
if(invalid_resource_name_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unsafe invalid-trial resource name")
endif()
if(NOT invalid_resource_name_error MATCHES
   "resource file '../resource.txt' is not a safe filename")
    message(FATAL_ERROR
        "summary reported an unexpected invalid-trial resource error: "
        "${invalid_resource_name_output}${invalid_resource_name_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix\n"
    "run-1,read-ratio,0.8,1,7,invalid,result\n"
    "run-1,read-ratio,0.8,2,8,invalid,result\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE duplicate_prefix_result
    OUTPUT_VARIABLE duplicate_prefix_output
    ERROR_VARIABLE duplicate_prefix_error
)
if(duplicate_prefix_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a reused output prefix")
endif()
if(NOT duplicate_prefix_error MATCHES "manifest contains duplicate output prefix 'result'")
    message(FATAL_ERROR
        "summary reported an unexpected duplicate prefix error: "
        "${duplicate_prefix_output}${duplicate_prefix_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,2,read-ratio,0.8,1,7,invalid,server-not-ready,result,result-server-resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE case_order_result
    OUTPUT_VARIABLE case_order_output
    ERROR_VARIABLE case_order_error
)
if(case_order_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a noncontiguous case order")
endif()
if(NOT case_order_error MATCHES "manifest case_order does not match row order")
    message(FATAL_ERROR
        "summary reported an unexpected case order error: "
        "${case_order_output}${case_order_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix,resource_file\n"
    "run-1,read-ratio,0.8,1,7,valid,result,result-server-resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE partial_production_result
    OUTPUT_VARIABLE partial_production_output
    ERROR_VARIABLE partial_production_error
)
if(partial_production_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted partial production columns")
endif()
if(NOT partial_production_error MATCHES "manifest is missing production columns")
    message(FATAL_ERROR
        "summary reported an unexpected production columns error: "
        "${partial_production_output}${partial_production_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix,notes\n"
    "run-1,read-ratio,0.8,1,7,valid,result,unreviewed\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unknown_column_result
    OUTPUT_VARIABLE unknown_column_output
    ERROR_VARIABLE unknown_column_error
)
if(unknown_column_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unknown manifest column")
endif()
if(NOT unknown_column_error MATCHES "manifest contains unknown columns: notes")
    message(FATAL_ERROR
        "summary reported an unexpected unknown column error: "
        "${unknown_column_output}${unknown_column_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,1,read-ratio,0.8,1,7,valid,server-not-ready,result,result-server-resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE reason_result
    OUTPUT_VARIABLE reason_output
    ERROR_VARIABLE reason_error
)
if(reason_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an inconsistent status reason")
endif()
if(NOT reason_error MATCHES "manifest valid trial reason must be 'none'")
    message(FATAL_ERROR
        "summary reported an unexpected reason error: ${reason_output}${reason_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,1,read-ratio,0.8,1,7,valid,none,result,other-server-resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE resource_binding_result
    OUTPUT_VARIABLE resource_binding_output
    ERROR_VARIABLE resource_binding_error
)
if(resource_binding_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a resource file from another trial")
endif()
if(NOT resource_binding_error MATCHES
   "manifest resource file does not match its output prefix")
    message(FATAL_ERROR
        "summary reported an unexpected resource binding error: "
        "${resource_binding_output}${resource_binding_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,1,read-ratio,0.8,1,7,valid,none,result,result-server-resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE resource_result
    OUTPUT_VARIABLE resource_output
    ERROR_VARIABLE resource_error
)
if(resource_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted missing resource evidence")
endif()
if(NOT resource_error MATCHES "resource evidence is missing: result-server-resource.txt")
    message(FATAL_ERROR
        "summary reported an unexpected resource evidence error: "
        "${resource_output}${resource_error}")
endif()

file(WRITE "${run_directory}/result-server-resource.txt" "captured_utc=2026-10-27T00:00:00Z\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE resource_content_result
    OUTPUT_VARIABLE resource_content_output
    ERROR_VARIABLE resource_content_error
)
if(resource_content_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted incomplete resource evidence")
endif()
if(NOT resource_content_error MATCHES "result-server-resource.txt is missing required fields")
    message(FATAL_ERROR
        "summary reported an unexpected resource content error: "
        "${resource_content_output}${resource_content_error}")
endif()
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-27T00:00:00Z\npid=42\nsystem=test system\n")
file(WRITE "${run_directory}/manifest.csv"
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,1,read-ratio,0.8,1,7,valid,none,result,result-server-resource.txt\n")
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2999-01-01T00:00:00Z\npid=42\nsystem=test system\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE future_resource_result
    OUTPUT_VARIABLE future_resource_output
    ERROR_VARIABLE future_resource_error
)
if(future_resource_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted future resource evidence")
endif()
if(NOT future_resource_error MATCHES "captured_utc is in the future")
    message(FATAL_ERROR
        "summary reported an unexpected future resource error: "
        "${future_resource_output}${future_resource_error}")
endif()
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-27T00:00:00Z\npid=42\nsystem=test system\n")
file(APPEND "${run_directory}/result-server-resource.txt" "unstructured resource output\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE malformed_resource_line_result
    OUTPUT_VARIABLE malformed_resource_line_output
    ERROR_VARIABLE malformed_resource_line_error
)
if(malformed_resource_line_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted malformed resource evidence")
endif()
if(NOT malformed_resource_line_error MATCHES "line 4 is not key-value evidence")
    message(FATAL_ERROR
        "summary reported an unexpected malformed resource error: "
        "${malformed_resource_line_output}${malformed_resource_line_error}")
endif()
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-27T00:00:00Z\npid=42\nsystem=test system\nbad field=value\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_resource_field_result
    OUTPUT_VARIABLE invalid_resource_field_output
    ERROR_VARIABLE invalid_resource_field_error
)
if(invalid_resource_field_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an invalid resource field name")
endif()
if(NOT invalid_resource_field_error MATCHES "line 4 has an invalid field name")
    message(FATAL_ERROR
        "summary reported an unexpected resource field error: "
        "${invalid_resource_field_output}${invalid_resource_field_error}")
endif()
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-27T00:00:00Z\npid=42\nsystem=test system\nVmRSS:=   \n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE empty_resource_value_result
    OUTPUT_VARIABLE empty_resource_value_output
    ERROR_VARIABLE empty_resource_value_error
)
if(empty_resource_value_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an empty resource value")
endif()
if(NOT empty_resource_value_error MATCHES "field 'VmRSS:' must not be empty")
    message(FATAL_ERROR
        "summary reported an unexpected empty resource value error: "
        "${empty_resource_value_output}${empty_resource_value_error}")
endif()
string(REPEAT "x" 65537 oversized_resource_evidence)
file(WRITE "${run_directory}/result-server-resource.txt" "${oversized_resource_evidence}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE oversized_resource_result
    OUTPUT_VARIABLE oversized_resource_output
    ERROR_VARIABLE oversized_resource_error
)
if(oversized_resource_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted oversized resource evidence")
endif()
if(NOT oversized_resource_error MATCHES "exceeds the resource evidence size limit")
    message(FATAL_ERROR
        "summary reported an unexpected oversized resource error: "
        "${oversized_resource_output}${oversized_resource_error}")
endif()
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-27T00:00:00Z\npid=42\nsystem=test system\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE production_schema_result
    OUTPUT_VARIABLE production_schema_output
    ERROR_VARIABLE production_schema_error
)
if(production_schema_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an incomplete production result")
endif()
if(NOT production_schema_error MATCHES "result.json is missing production fields")
    message(FATAL_ERROR
        "summary reported an unexpected production schema error: "
        "${production_schema_output}${production_schema_error}")
endif()

file(WRITE "${run_directory}/result.json"
    "{\"timestamp_utc\":\"2026-10-27T00:00:00Z\","
    "\"git_sha\":\"0123456789abcdef0123456789abcdef01234567\","
    "\"working_tree_dirty\":false,\"compiler\":\"Clang 17\","
    "\"build\":\"Release\",\"system\":\"test system\",\"hardware_threads\":8,"
    "\"ram\":\"test ram\",\"storage_medium\":\"ssd\","
    "\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"host\":\"127.0.0.1\",\"port\":7391,"
    "\"connections\":1,\"threads\":1,\"request_bound\":2,\"duration_bound_s\":0,"
    "\"read_ratio\":0.8,\"key_count\":100,\"value_size\":10,\"pipeline_depth\":1,"
    "\"warmup_requests\":0,\"preload\":false,\"server_workers\":1,"
    "\"server_shards\":1,\"durability\":\"periodic\","
    "\"repetition\":1,\"seed\":7,\"operations\":2,\"seconds\":1,"
    "\"operations_per_second\":2,"
    "\"latency_us\":{\"p50\":1,\"p95\":2,\"p99\":2,\"max\":2},"
    "\"errors\":0,\"connection_errors\":0}\n")
file(READ "${run_directory}/result.json" production_result_json)
string(REPEAT "x" 65537 oversized_result_evidence)
file(WRITE "${run_directory}/result.json" "${oversized_result_evidence}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE oversized_result_result
    OUTPUT_VARIABLE oversized_result_output
    ERROR_VARIABLE oversized_result_error
)
if(oversized_result_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted oversized result evidence")
endif()
if(NOT oversized_result_error MATCHES "exceeds the result evidence size limit")
    message(FATAL_ERROR
        "summary reported an unexpected result size error: "
        "${oversized_result_output}${oversized_result_error}")
endif()
file(WRITE "${run_directory}/result.json" "${production_result_json}")
string(REPLACE
    "\"connection_errors\":0}"
    "\"connection_errors\":0,\"notes\":\"unreviewed\"}"
    unknown_result_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${unknown_result_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unknown_result_field_result
    OUTPUT_VARIABLE unknown_result_field_output
    ERROR_VARIABLE unknown_result_field_error
)
if(unknown_result_field_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unknown JSON result field")
endif()
if(NOT unknown_result_field_error MATCHES
   "result.json contains unknown production fields: notes")
    message(FATAL_ERROR
        "summary reported an unexpected JSON schema error: "
        "${unknown_result_field_output}${unknown_result_field_error}")
endif()
file(WRITE "${run_directory}/result.json" "${production_result_json}")
string(REPLACE
    "\"max\":2}"
    "\"max\":2,\"p999\":2}"
    unknown_latency_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${unknown_latency_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unknown_latency_result
    OUTPUT_VARIABLE unknown_latency_output
    ERROR_VARIABLE unknown_latency_error
)
if(unknown_latency_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unknown latency field")
endif()
if(NOT unknown_latency_error MATCHES
   "result.json contains unknown latency fields: p999")
    message(FATAL_ERROR
        "summary reported an unexpected latency schema error: "
        "${unknown_latency_output}${unknown_latency_error}")
endif()
file(WRITE "${run_directory}/result.json" "${production_result_json}")
string(REPLACE
    "2026-10-27T00:00:00Z"
    "2999-10-27T00:00:00Z"
    future_result_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${future_result_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE future_result_result
    OUTPUT_VARIABLE future_result_output
    ERROR_VARIABLE future_result_error
)
if(future_result_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a future result timestamp")
endif()
if(NOT future_result_error MATCHES "result.json timestamp_utc is in the future")
    message(FATAL_ERROR
        "summary reported an unexpected result timestamp error: "
        "${future_result_output}${future_result_error}")
endif()
file(WRITE "${run_directory}/result.json" "${production_result_json}")
string(REPLACE
    "\"host\":\"127.0.0.1\""
    "\"host\":\"   \""
    invalid_host_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${invalid_host_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_host_result
    OUTPUT_VARIABLE invalid_host_output
    ERROR_VARIABLE invalid_host_error
)
if(invalid_host_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a blank result host")
endif()
if(NOT invalid_host_error MATCHES "result.json host must be single-line text")
    message(FATAL_ERROR
        "summary reported an unexpected result host error: "
        "${invalid_host_output}${invalid_host_error}")
endif()
string(REPLACE
    "\"port\":7391"
    "\"port\":70000"
    invalid_port_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${invalid_port_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_port_result
    OUTPUT_VARIABLE invalid_port_output
    ERROR_VARIABLE invalid_port_error
)
if(invalid_port_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an out-of-range result port")
endif()
if(NOT invalid_port_error MATCHES
   "result.json port must be an integer from 1 to 65535")
    message(FATAL_ERROR
        "summary reported an unexpected result port error: "
        "${invalid_port_output}${invalid_port_error}")
endif()
string(REPLACE
    "\"request_bound\":2"
    "\"request_bound\":0"
    missing_workload_bound_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${missing_workload_bound_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE missing_workload_bound_result
    OUTPUT_VARIABLE missing_workload_bound_output
    ERROR_VARIABLE missing_workload_bound_error
)
if(missing_workload_bound_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unbounded workload")
endif()
if(NOT missing_workload_bound_error MATCHES
   "request_bound and duration_bound_s cannot both be zero")
    message(FATAL_ERROR
        "summary reported an unexpected workload bound error: "
        "${missing_workload_bound_output}${missing_workload_bound_error}")
endif()
string(REPLACE
    "\"operations\":2,\"seconds\":1,\"operations_per_second\":2"
    "\"operations\":3,\"seconds\":1,\"operations_per_second\":3"
    excessive_operations_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${excessive_operations_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE excessive_operations_result
    OUTPUT_VARIABLE excessive_operations_output
    ERROR_VARIABLE excessive_operations_error
)
if(excessive_operations_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted more operations than requested")
endif()
if(NOT excessive_operations_error MATCHES "operations cannot exceed request_bound")
    message(FATAL_ERROR
        "summary reported an unexpected completed operation error: "
        "${excessive_operations_output}${excessive_operations_error}")
endif()
string(REPLACE
    "\"value_size\":10"
    "\"value_size\":16777217"
    oversized_value_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${oversized_value_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE oversized_value_result
    OUTPUT_VARIABLE oversized_value_output
    ERROR_VARIABLE oversized_value_error
)
if(oversized_value_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an oversized benchmark value")
endif()
if(NOT oversized_value_error MATCHES "value_size exceeds the protocol limit")
    message(FATAL_ERROR
        "summary reported an unexpected value size error: "
        "${oversized_value_output}${oversized_value_error}")
endif()
string(REPLACE
    "\"warmup_requests\":0,\"preload\":false"
    "\"warmup_requests\":18446744073709551615,\"preload\":true"
    exhausted_request_ids_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${exhausted_request_ids_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE exhausted_request_ids_result
    OUTPUT_VARIABLE exhausted_request_ids_output
    ERROR_VARIABLE exhausted_request_ids_error
)
if(exhausted_request_ids_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted exhausted request identifiers")
endif()
if(NOT exhausted_request_ids_error MATCHES "workload exhausts request identifiers")
    message(FATAL_ERROR
        "summary reported an unexpected request identifier error: "
        "${exhausted_request_ids_output}${exhausted_request_ids_error}")
endif()
string(REPEAT "r" 4097 oversized_ram)
string(REPLACE
    "\"ram\":\"test ram\""
    "\"ram\":\"${oversized_ram}\""
    oversized_metadata_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${oversized_metadata_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE oversized_metadata_result
    OUTPUT_VARIABLE oversized_metadata_output
    ERROR_VARIABLE oversized_metadata_error
)
if(oversized_metadata_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted oversized result metadata")
endif()
if(NOT oversized_metadata_error MATCHES "ram exceeds the metadata length limit")
    message(FATAL_ERROR
        "summary reported an unexpected metadata length error: "
        "${oversized_metadata_output}${oversized_metadata_error}")
endif()
string(REPLACE
    "\"connections\":1,\"threads\":1"
    "\"connections\":1,\"threads\":2"
    excess_threads_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${excess_threads_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE excess_threads_result
    OUTPUT_VARIABLE excess_threads_output
    ERROR_VARIABLE excess_threads_error
)
if(excess_threads_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted more threads than connections")
endif()
if(NOT excess_threads_error MATCHES "threads cannot exceed connections")
    message(FATAL_ERROR
        "summary reported an unexpected thread topology error: "
        "${excess_threads_output}${excess_threads_error}")
endif()
string(REPLACE
    "\"warmup_requests\":0,\"preload\":false"
    "\"warmup_requests\":1,\"preload\":false"
    warmup_without_preload_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${warmup_without_preload_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE warmup_without_preload_result
    OUTPUT_VARIABLE warmup_without_preload_output
    ERROR_VARIABLE warmup_without_preload_error
)
if(warmup_without_preload_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted warmup without preload")
endif()
if(NOT warmup_without_preload_error MATCHES
   "warmup_requests require dataset preload")
    message(FATAL_ERROR
        "summary reported an unexpected warmup error: "
        "${warmup_without_preload_output}${warmup_without_preload_error}")
endif()
string(REPLACE
    "\"errors\":0,\"connection_errors\":0"
    "\"errors\":3,\"connection_errors\":0"
    excessive_errors_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${excessive_errors_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE excessive_errors_result
    OUTPUT_VARIABLE excessive_errors_output
    ERROR_VARIABLE excessive_errors_error
)
if(excessive_errors_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted more errors than operations")
endif()
if(NOT excessive_errors_error MATCHES
   "errors cannot exceed completed operations")
    message(FATAL_ERROR
        "summary reported an unexpected operation error count error: "
        "${excessive_errors_output}${excessive_errors_error}")
endif()
string(REPLACE
    "\"errors\":0,\"connection_errors\":0"
    "\"errors\":0,\"connection_errors\":2"
    excessive_connection_errors_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${excessive_connection_errors_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE excessive_connection_errors_result
    OUTPUT_VARIABLE excessive_connection_errors_output
    ERROR_VARIABLE excessive_connection_errors_error
)
if(excessive_connection_errors_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted too many connection errors")
endif()
if(NOT excessive_connection_errors_error MATCHES
   "connection_errors cannot exceed worker threads")
    message(FATAL_ERROR
        "summary reported an unexpected connection error count error: "
        "${excessive_connection_errors_output}${excessive_connection_errors_error}")
endif()
file(WRITE "${run_directory}/result.json" "${production_result_json}")
string(REPLACE
    "\"durability\":\"periodic\""
    "\"durability\":\"async\""
    invalid_durability_json
    "${production_result_json}")
file(WRITE "${run_directory}/result.json" "${invalid_durability_json}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE invalid_durability_result
    OUTPUT_VARIABLE invalid_durability_output
    ERROR_VARIABLE invalid_durability_error
)
if(invalid_durability_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted unsupported durability metadata")
endif()
if(NOT invalid_durability_error MATCHES "result.json durability is unsupported")
    message(FATAL_ERROR
        "summary reported an unexpected durability error: "
        "${invalid_durability_output}${invalid_durability_error}")
endif()
file(WRITE "${run_directory}/result.json" "${production_result_json}")
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-26T23:59:59Z\npid=42\nsystem=test system\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE stale_resource_result
    OUTPUT_VARIABLE stale_resource_output
    ERROR_VARIABLE stale_resource_error
)
if(stale_resource_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted resource evidence predating the result")
endif()
if(NOT stale_resource_error MATCHES "predates benchmark result")
    message(FATAL_ERROR
        "summary reported an unexpected stale resource error: "
        "${stale_resource_output}${stale_resource_error}")
endif()
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-27T00:10:00Z\npid=42\nsystem=test system\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE delayed_resource_result
    OUTPUT_VARIABLE delayed_resource_output
    ERROR_VARIABLE delayed_resource_error
)
if(delayed_resource_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a delayed resource capture")
endif()
if(NOT delayed_resource_error MATCHES "captured too long after benchmark result")
    message(FATAL_ERROR
        "summary reported an unexpected delayed resource error: "
        "${delayed_resource_output}${delayed_resource_error}")
endif()
file(WRITE "${run_directory}/result-server-resource.txt"
    "captured_utc=2026-10-27T00:00:00Z\npid=42\nsystem=test system\n")
string(REPEAT "1\n" 66 oversized_latency_evidence)
file(WRITE "${run_directory}/result-latency-us.csv"
    "latency_us\n${oversized_latency_evidence}")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE oversized_latency_result
    OUTPUT_VARIABLE oversized_latency_output
    ERROR_VARIABLE oversized_latency_error
)
if(oversized_latency_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted oversized latency evidence")
endif()
if(NOT oversized_latency_error MATCHES "exceeds the latency evidence size limit")
    message(FATAL_ERROR
        "summary reported an unexpected latency size error: "
        "${oversized_latency_output}${oversized_latency_error}")
endif()
file(REMOVE "${run_directory}/result-latency-us.csv")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE latency_evidence_result
    OUTPUT_VARIABLE latency_evidence_output
    ERROR_VARIABLE latency_evidence_error
)
if(latency_evidence_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted missing latency evidence")
endif()
if(NOT latency_evidence_error MATCHES
   "latency evidence is missing: result-latency-us.csv")
    message(FATAL_ERROR
        "summary reported an unexpected latency evidence error: "
        "${latency_evidence_output}${latency_evidence_error}")
endif()

file(WRITE "${run_directory}/result-latency-us.csv" "latency_us\n1\n2\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE csv_evidence_result
    OUTPUT_VARIABLE csv_evidence_output
    ERROR_VARIABLE csv_evidence_error
)
if(csv_evidence_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted missing CSV evidence")
endif()
if(NOT csv_evidence_error MATCHES "CSV evidence is missing: result.csv")
    message(FATAL_ERROR
        "summary reported an unexpected CSV evidence error: "
        "${csv_evidence_output}${csv_evidence_error}")
endif()

file(WRITE "${run_directory}/result.csv"
    "timestamp_utc,run_id,experiment,variant,git_sha,working_tree_dirty,compiler,build,system,"
    "ram,storage_medium,connections,threads,requests,duration_s,read_ratio,key_count,value_size,"
    "pipeline_depth,warmup,preload,workers,shards,durability,seed,repetition,operations,seconds,"
    "ops_per_second,p50_us,p95_us,p99_us,max_us,errors,connection_errors,notes\n"
    "2026-10-27T00:00:00Z,run-1,read-ratio,0.8,"
    "0123456789abcdef0123456789abcdef01234567,0,Clang 17,Release,test system,"
    "test ram,ssd,1,1,2,0,0.8,100,10,1,0,0,1,1,periodic,"
    "7,1,2,1,2,1,2,2,2,0,0,unreviewed\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE unknown_csv_column_result
    OUTPUT_VARIABLE unknown_csv_column_output
    ERROR_VARIABLE unknown_csv_column_error
)
if(unknown_csv_column_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted an unknown CSV evidence column")
endif()
if(NOT unknown_csv_column_error MATCHES "result.csv contains unknown columns: notes")
    message(FATAL_ERROR
        "summary reported an unexpected CSV column error: "
        "${unknown_csv_column_output}${unknown_csv_column_error}")
endif()

file(WRITE "${run_directory}/result.csv"
    "timestamp_utc,run_id,experiment,variant,git_sha,working_tree_dirty,compiler,build,system,"
    "ram,storage_medium,connections,threads,requests,duration_s,read_ratio,key_count,value_size,"
    "pipeline_depth,warmup,preload,workers,shards,durability,seed,repetition,operations,seconds,"
    "ops_per_second,p50_us,p95_us,p99_us,max_us,errors,"
    "connection_errors\n"
    "2026-10-27T00:00:00Z,run-1,read-ratio,0.8,"
    "0123456789abcdef0123456789abcdef01234567,0,Clang 17,Release,test system,"
    "test ram,ssd,2,1,2,0,0.8,100,10,1,0,0,1,1,periodic,"
    "7,1,2,1,2,1,2,2,2,0,0\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE workload_result
    OUTPUT_VARIABLE workload_output
    ERROR_VARIABLE workload_error
)
if(workload_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted mismatched workload evidence")
endif()
if(NOT workload_error MATCHES "result.csv connections does not match JSON result")
    message(FATAL_ERROR
        "summary reported an unexpected workload error: ${workload_output}${workload_error}")
endif()
file(WRITE "${run_directory}/result.csv"
    "timestamp_utc,run_id,experiment,variant,git_sha,working_tree_dirty,compiler,build,system,"
    "ram,storage_medium,connections,threads,requests,duration_s,read_ratio,key_count,value_size,"
    "pipeline_depth,warmup,preload,workers,shards,durability,seed,repetition,operations,seconds,"
    "ops_per_second,p50_us,p95_us,p99_us,max_us,errors,connection_errors\n"
    "2026-10-27T00:00:00Z,run-1,read-ratio,0.8,"
    "0123456789abcdef0123456789abcdef01234567,0,Clang 17,Release,test system,"
    "test ram,ssd,1,1,2,0,0.8,100,10,1,0,0,1,1,periodic,"
    "7,1,2,1,2,1,2,2,2,0,0\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE production_result
    OUTPUT_VARIABLE production_output
    ERROR_VARIABLE production_error
)
if(NOT production_result EQUAL 0 OR NOT EXISTS "${run_directory}/summary.csv")
    message(FATAL_ERROR
        "complete production benchmark evidence failed: "
        "${production_output}${production_error}")
endif()
file(REMOVE "${run_directory}/summary.csv")

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

file(WRITE "${run_directory}/summary.csv" "existing-summary\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE existing_result
    OUTPUT_VARIABLE existing_output
    ERROR_VARIABLE existing_error
)
if(existing_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary replaced existing evidence")
endif()
if(NOT existing_error MATCHES "failed to publish benchmark summary")
    message(FATAL_ERROR
        "summary reported an unexpected publication error: ${existing_output}${existing_error}")
endif()
file(READ "${run_directory}/summary.csv" preserved_summary)
if(NOT preserved_summary STREQUAL "existing-summary\n")
    message(FATAL_ERROR "benchmark summary changed existing evidence")
endif()
file(GLOB temporary_summaries "${run_directory}/.summary.*.tmp")
if(temporary_summaries)
    message(FATAL_ERROR "benchmark summary leaked temporary output: ${temporary_summaries}")
endif()
