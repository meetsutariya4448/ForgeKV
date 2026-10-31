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
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,2,read-ratio,0.8,1,7,invalid,server-not-ready,result,result-resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE case_order_result
    OUTPUT_VARIABLE case_order_output
    ERROR_VARIABLE case_order_error
)
if(case_order_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted a noncontiguous case order")
endif()
if(NOT case_order_error MATCHES "manifest case_order is not contiguous")
    message(FATAL_ERROR
        "summary reported an unexpected case order error: "
        "${case_order_output}${case_order_error}")
endif()

file(WRITE "${run_directory}/manifest.csv"
    "run_id,experiment,variant,trial,seed,status,output_prefix,resource_file\n"
    "run-1,read-ratio,0.8,1,7,valid,result,result-resource.txt\n")
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
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,1,read-ratio,0.8,1,7,valid,none,result,result-resource.txt\n")
execute_process(
    COMMAND python3 "${SUMMARY_SCRIPT}" "${run_directory}"
    RESULT_VARIABLE resource_result
    OUTPUT_VARIABLE resource_output
    ERROR_VARIABLE resource_error
)
if(resource_result EQUAL 0)
    message(FATAL_ERROR "benchmark summary accepted missing resource evidence")
endif()
if(NOT resource_error MATCHES "resource evidence is missing: result-resource.txt")
    message(FATAL_ERROR
        "summary reported an unexpected resource evidence error: "
        "${resource_output}${resource_error}")
endif()

file(WRITE "${run_directory}/result-resource.txt" "captured_utc=2026-10-27T00:00:00Z\n")
file(WRITE "${run_directory}/manifest.csv"
    "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file\n"
    "run-1,1,read-ratio,0.8,1,7,valid,none,result,result-resource.txt\n")
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
    "\"run_id\":\"run-1\",\"experiment\":\"read-ratio\",\"variant\":\"0.8\","
    "\"repetition\":1,\"seed\":7,\"operations\":2,\"seconds\":1,"
    "\"operations_per_second\":2,"
    "\"latency_us\":{\"p50\":1,\"p95\":2,\"p99\":2,\"max\":2},"
    "\"errors\":0,\"connection_errors\":0}\n")
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
    "seed,repetition,operations,seconds,ops_per_second,p50_us,p95_us,p99_us,max_us,errors,"
    "connection_errors\n"
    "2026-10-27T00:00:00Z,run-1,read-ratio,0.8,"
    "0123456789abcdef0123456789abcdef01234567,0,Clang 17,Release,test system,"
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
