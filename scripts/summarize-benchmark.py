#!/usr/bin/env python3
"""Summarize immutable ForgeKV benchmark trial JSON without third-party packages."""

import csv
import datetime as dt
import json
import math
import os
import re
import statistics
import sys
import tempfile
from pathlib import Path


REQUIRED_MANIFEST_FIELDS = {
    "run_id", "experiment", "variant", "trial", "seed", "status", "output_prefix",
}
PRODUCTION_MANIFEST_FIELDS = {"case_order", "reason", "resource_file"}
REQUIRED_PRODUCTION_RESULT_FIELDS = {
    "timestamp_utc", "git_sha", "working_tree_dirty", "compiler", "build", "system",
    "hardware_threads", "operations", "seconds", "operations_per_second", "latency_us",
    "errors", "connection_errors", "ram", "storage_medium", "connections", "threads",
    "request_bound", "duration_bound_s", "read_ratio", "key_count", "value_size",
    "pipeline_depth", "warmup_requests", "preload", "server_workers", "server_shards",
    "durability", "host", "port",
}
PRODUCTION_RESULT_FIELDS = REQUIRED_PRODUCTION_RESULT_FIELDS | {
    "run_id", "experiment", "variant", "repetition", "seed",
}
MAX_RESOURCE_EVIDENCE_BYTES = 64 * 1024
MAX_MANIFEST_EVIDENCE_BYTES = 1024 * 1024
MAX_BENCHMARK_VALUE_BYTES = 16 * 1024 * 1024
MAX_REQUEST_ID = (1 << 64) - 1
MAX_RESULT_TEXT_CHARS = 4096
MAX_RESOURCE_CAPTURE_DELAY = dt.timedelta(minutes=5)


def unique_json_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field {key!r}")
        result[key] = value
    return result


def validate_result_metadata(
    result_path: Path, row: dict[str, str], result: dict, require_complete: bool = False
) -> None:
    if require_complete:
        missing_fields = sorted(REQUIRED_PRODUCTION_RESULT_FIELDS - result.keys())
        if missing_fields:
            raise ValueError(
                f"{result_path.name} is missing production fields: {', '.join(missing_fields)}"
            )
        unknown_fields = sorted(result.keys() - PRODUCTION_RESULT_FIELDS)
        if unknown_fields:
            raise ValueError(
                f"{result_path.name} contains unknown production fields: "
                f"{', '.join(unknown_fields)}"
            )
        latency = result.get("latency_us")
        missing_latency = sorted(
            {"p50", "p95", "p99", "max"}
            - (latency.keys() if isinstance(latency, dict) else set())
        )
        if missing_latency:
            raise ValueError(
                f"{result_path.name} is missing latency fields: {', '.join(missing_latency)}"
            )
        unknown_latency = sorted(latency.keys() - {"p50", "p95", "p99", "max"})
        if unknown_latency:
            raise ValueError(
                f"{result_path.name} contains unknown latency fields: "
                f"{', '.join(unknown_latency)}"
            )
        for field in (
            "connections", "threads", "key_count", "value_size",
            "pipeline_depth", "server_workers", "server_shards",
        ):
            value = result[field]
            if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
                raise ValueError(f"{result_path.name} {field} must be a positive integer")
        if result["value_size"] > MAX_BENCHMARK_VALUE_BYTES:
            raise ValueError(
                f"{result_path.name} value_size exceeds the protocol limit"
            )
        if result["threads"] > result["connections"]:
            raise ValueError(
                f"{result_path.name} threads cannot exceed connections"
            )
        request_bound = result["request_bound"]
        if (
            isinstance(request_bound, bool)
            or not isinstance(request_bound, int)
            or request_bound < 0
        ):
            raise ValueError(
                f"{result_path.name} request_bound must be a nonnegative integer"
            )
        host = result["host"]
        if not isinstance(host, str) or not host.strip() or "\n" in host or "\r" in host:
            raise ValueError(f"{result_path.name} host must be single-line text")
        if len(host) > MAX_RESULT_TEXT_CHARS:
            raise ValueError(f"{result_path.name} host exceeds the metadata length limit")
        port = result["port"]
        if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
            raise ValueError(f"{result_path.name} port must be an integer from 1 to 65535")
        warmup_requests = result["warmup_requests"]
        if (
            isinstance(warmup_requests, bool)
            or not isinstance(warmup_requests, int)
            or warmup_requests < 0
        ):
            raise ValueError(
                f"{result_path.name} warmup_requests must be a nonnegative integer"
            )
        duration = result["duration_bound_s"]
        if isinstance(duration, bool) or not isinstance(duration, int) or duration < 0:
            raise ValueError(
                f"{result_path.name} duration_bound_s must be a nonnegative integer"
            )
        if request_bound == 0 and duration == 0:
            raise ValueError(
                f"{result_path.name} request_bound and duration_bound_s cannot both be zero"
            )
        read_ratio = result["read_ratio"]
        if (
            isinstance(read_ratio, bool)
            or not isinstance(read_ratio, (int, float))
            or not math.isfinite(read_ratio)
            or not 0 <= read_ratio <= 1
        ):
            raise ValueError(f"{result_path.name} read_ratio must be between zero and one")
        if not isinstance(result["preload"], bool):
            raise ValueError(f"{result_path.name} preload must be boolean")
        if not result["preload"] and warmup_requests != 0:
            raise ValueError(
                f"{result_path.name} warmup_requests require dataset preload"
            )
        setup_requests = result["key_count"] + warmup_requests if result["preload"] else 0
        if (
            setup_requests > MAX_REQUEST_ID
            or request_bound > MAX_REQUEST_ID - setup_requests
        ):
            raise ValueError(
                f"{result_path.name} workload exhausts request identifiers"
            )
        for field in ("ram", "storage_medium", "durability"):
            value = result[field]
            if not isinstance(value, str) or not value.strip() or "\n" in value or "\r" in value:
                raise ValueError(f"{result_path.name} {field} must be single-line text")
            if len(value) > MAX_RESULT_TEXT_CHARS:
                raise ValueError(
                    f"{result_path.name} {field} exceeds the metadata length limit"
                )
        if result["durability"] not in {"unspecified", "always", "periodic", "none"}:
            raise ValueError(f"{result_path.name} durability is unsupported")

    repetition = int(row["trial"])
    seed = int(row["seed"])
    expected = {
        "run_id": row["run_id"],
        "experiment": row["experiment"],
        "variant": row["variant"],
        "repetition": repetition,
        "seed": seed,
    }
    for field, expected_value in expected.items():
        actual = result.get(field)
        if field in {"repetition", "seed"} and (
            isinstance(actual, bool) or not isinstance(actual, int)
        ):
            raise ValueError(f"{result_path.name} {field} must be an integer")
        if actual != expected_value:
            raise ValueError(
                f"{result_path.name} {field}={actual!r} does not match "
                f"manifest value {expected_value!r}"
            )

    numeric_fields = {
        "operations_per_second": result.get("operations_per_second"),
        "latency_us.p99": result.get("latency_us", {}).get("p99")
        if isinstance(result.get("latency_us"), dict) else None,
    }
    for field, value in numeric_fields.items():
        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
            or value < 0
        ):
            raise ValueError(f"{result_path.name} {field} must be a finite nonnegative number")

    if "operations" in result or "seconds" in result:
        operations = result.get("operations")
        seconds = result.get("seconds")
        if isinstance(operations, bool) or not isinstance(operations, int) or operations <= 0:
            raise ValueError(f"{result_path.name} operations must be a positive integer")
        if (
            isinstance(seconds, bool)
            or not isinstance(seconds, (int, float))
            or not math.isfinite(seconds)
            or seconds <= 0
        ):
            raise ValueError(f"{result_path.name} seconds must be a finite positive number")
        if not math.isclose(
            float(result["operations_per_second"]), operations / seconds, rel_tol=1e-5
        ):
            raise ValueError(
                f"{result_path.name} throughput is inconsistent with operations and seconds"
            )
        request_bound = result.get("request_bound", 0)
        if request_bound > 0 and operations > request_bound:
            raise ValueError(
                f"{result_path.name} operations cannot exceed request_bound"
            )

    latency = result.get("latency_us")
    if isinstance(latency, dict) and any(
        field in latency for field in ("p50", "p95", "max")
    ):
        ordered_latency_fields = ("p50", "p95", "p99", "max")
        ordered_latencies = []
        for field in ordered_latency_fields:
            value = latency.get(field)
            if (
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
                or value < 0
            ):
                raise ValueError(
                    f"{result_path.name} latency_us.{field} must be a finite nonnegative number"
                )
            ordered_latencies.append(float(value))
        if ordered_latencies != sorted(ordered_latencies):
            raise ValueError(f"{result_path.name} latency percentiles are not ordered")

    provenance_fields = {
        "timestamp_utc", "git_sha", "working_tree_dirty", "compiler", "build", "system",
        "hardware_threads",
    }
    if provenance_fields.intersection(result):
        missing_provenance = sorted(provenance_fields - result.keys())
        if missing_provenance:
            raise ValueError(
                f"{result_path.name} is missing provenance fields: "
                f"{', '.join(missing_provenance)}"
            )
        try:
            timestamp = dt.datetime.strptime(result["timestamp_utc"], "%Y-%m-%dT%H:%M:%SZ")
        except (TypeError, ValueError) as error:
            raise ValueError(f"{result_path.name} timestamp_utc is invalid") from error
        if timestamp.strftime("%Y-%m-%dT%H:%M:%SZ") != result["timestamp_utc"]:
            raise ValueError(f"{result_path.name} timestamp_utc is invalid")
        if timestamp > dt.datetime.utcnow() + dt.timedelta(minutes=5):
            raise ValueError(f"{result_path.name} timestamp_utc is in the future")
        if not isinstance(result["git_sha"], str) or not re.fullmatch(
            r"[0-9a-f]{40}", result["git_sha"]
        ):
            raise ValueError(f"{result_path.name} git_sha must be a 40-character commit")
        if not isinstance(result["working_tree_dirty"], bool):
            raise ValueError(f"{result_path.name} working_tree_dirty must be boolean")
        for field in ("compiler", "build", "system"):
            value = result[field]
            if not isinstance(value, str) or not value.strip() or "\n" in value or "\r" in value:
                raise ValueError(f"{result_path.name} {field} must be single-line text")
            if len(value) > MAX_RESULT_TEXT_CHARS:
                raise ValueError(
                    f"{result_path.name} {field} exceeds the metadata length limit"
                )
        hardware_threads = result["hardware_threads"]
        if (
            isinstance(hardware_threads, bool)
            or not isinstance(hardware_threads, int)
            or hardware_threads < 0
        ):
            raise ValueError(
                f"{result_path.name} hardware_threads must be a nonnegative integer"
            )

    for field in ("errors", "connection_errors"):
        value = result.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"{result_path.name} {field} must be a nonnegative integer")
    if "operations" in result and result["errors"] > result["operations"]:
        raise ValueError(
            f"{result_path.name} errors cannot exceed completed operations"
        )
    if "threads" in result and result["connection_errors"] > result["threads"]:
        raise ValueError(
            f"{result_path.name} connection_errors cannot exceed worker threads"
        )


def validate_latency_evidence(
    run_directory: Path, output_prefix: str, result: dict
) -> None:
    latency_path = run_directory / f"{output_prefix}-latency-us.csv"
    if latency_path.is_symlink():
        raise ValueError(f"{latency_path.name} must not be a symbolic link")
    try:
        with latency_path.open(newline="", encoding="utf-8") as latency_file:
            reader = csv.reader(latency_file)
            if next(reader, None) != ["latency_us"]:
                raise ValueError(f"{latency_path.name} has an invalid header")
            samples = []
            for row_number, row in enumerate(reader, start=2):
                if len(row) != 1:
                    raise ValueError(
                        f"{latency_path.name} row {row_number} must contain one value"
                    )
                try:
                    sample = float(row[0])
                except ValueError as error:
                    raise ValueError(
                        f"{latency_path.name} row {row_number} is not numeric"
                    ) from error
                if not math.isfinite(sample) or sample < 0:
                    raise ValueError(
                        f"{latency_path.name} row {row_number} must be finite and nonnegative"
                    )
                samples.append(sample)
    except FileNotFoundError as error:
        raise ValueError(f"latency evidence is missing: {latency_path.name}") from error

    if len(samples) != result["operations"]:
        raise ValueError(
            f"{latency_path.name} sample count does not match operations"
        )
    samples.sort()
    for field, fraction in (("p50", 0.50), ("p95", 0.95), ("p99", 0.99)):
        index = max(1, math.ceil(fraction * len(samples))) - 1
        if not math.isclose(
            samples[index], float(result["latency_us"][field]), rel_tol=1e-5
        ):
            raise ValueError(
                f"{latency_path.name} {field} does not match JSON result"
            )
    if not math.isclose(
        samples[-1], float(result["latency_us"]["max"]), rel_tol=1e-5
    ):
        raise ValueError(f"{latency_path.name} max does not match JSON result")


def validate_resource_evidence(resource_path: Path) -> dt.datetime:
    if resource_path.stat().st_size > MAX_RESOURCE_EVIDENCE_BYTES:
        raise ValueError(
            f"{resource_path.name} exceeds the resource evidence size limit"
        )
    values: dict[str, str] = {}
    for line_number, line in enumerate(
        resource_path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if "=" not in line:
            raise ValueError(
                f"{resource_path.name} line {line_number} is not key-value evidence"
            )
        key, value = line.split("=", 1)
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*:?", key):
            raise ValueError(
                f"{resource_path.name} line {line_number} has an invalid field name"
            )
        if not value.strip():
            raise ValueError(
                f"{resource_path.name} field {key!r} must not be empty"
            )
        if key in values:
            raise ValueError(f"{resource_path.name} contains duplicate field {key!r}")
        values[key] = value
    missing_fields = sorted({"captured_utc", "pid", "system"} - values.keys())
    if missing_fields:
        raise ValueError(
            f"{resource_path.name} is missing required fields: {', '.join(missing_fields)}"
        )
    try:
        timestamp = dt.datetime.strptime(values["captured_utc"], "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as error:
        raise ValueError(f"{resource_path.name} captured_utc is invalid") from error
    if timestamp.strftime("%Y-%m-%dT%H:%M:%SZ") != values["captured_utc"]:
        raise ValueError(f"{resource_path.name} captured_utc is invalid")
    if timestamp > dt.datetime.utcnow() + dt.timedelta(minutes=5):
        raise ValueError(f"{resource_path.name} captured_utc is in the future")
    if not re.fullmatch(r"[1-9][0-9]*", values["pid"]):
        raise ValueError(f"{resource_path.name} pid must be a positive integer")
    if not values["system"].strip() or "\r" in values["system"]:
        raise ValueError(f"{resource_path.name} system must be nonempty text")
    return timestamp


def validate_csv_evidence(
    run_directory: Path, output_prefix: str, result: dict
) -> None:
    csv_path = run_directory / f"{output_prefix}.csv"
    if csv_path.is_symlink():
        raise ValueError(f"{csv_path.name} must not be a symbolic link")
    try:
        with csv_path.open(newline="", encoding="utf-8") as csv_file:
            reader = csv.DictReader(csv_file)
            fields = reader.fieldnames or []
            if len(fields) != len(set(fields)):
                raise ValueError(f"{csv_path.name} contains duplicate columns")
            required_fields = {
                "timestamp_utc", "run_id", "experiment", "variant", "git_sha",
                "working_tree_dirty", "compiler", "build", "system", "seed",
                "repetition", "operations", "seconds", "ops_per_second", "p50_us",
                "p95_us", "p99_us", "max_us", "errors", "connection_errors",
                "ram", "storage_medium", "connections", "threads", "requests",
                "duration_s", "read_ratio", "key_count", "value_size", "pipeline_depth",
                "warmup", "preload", "workers", "shards", "durability",
            }
            missing_fields = sorted(required_fields - set(fields))
            if missing_fields:
                raise ValueError(
                    f"{csv_path.name} is missing columns: {', '.join(missing_fields)}"
                )
            unknown_fields = sorted(set(fields) - required_fields)
            if unknown_fields:
                raise ValueError(
                    f"{csv_path.name} contains unknown columns: "
                    + ", ".join(unknown_fields)
                )
            rows = list(reader)
    except FileNotFoundError as error:
        raise ValueError(f"CSV evidence is missing: {csv_path.name}") from error
    if len(rows) != 1 or None in rows[0]:
        raise ValueError(f"{csv_path.name} must contain exactly one complete result row")
    row = rows[0]

    for field in (
        "timestamp_utc", "run_id", "experiment", "variant", "git_sha", "compiler",
        "build", "system", "ram", "storage_medium", "durability",
    ):
        if row[field] != str(result[field]):
            raise ValueError(f"{csv_path.name} {field} does not match JSON result")
    expected_integers = {
        "working_tree_dirty": int(result["working_tree_dirty"]),
        "seed": result["seed"],
        "repetition": result["repetition"],
        "operations": result["operations"],
        "errors": result["errors"],
        "connection_errors": result["connection_errors"],
        "connections": result["connections"],
        "threads": result["threads"],
        "requests": result["request_bound"],
        "key_count": result["key_count"],
        "value_size": result["value_size"],
        "pipeline_depth": result["pipeline_depth"],
        "warmup": result["warmup_requests"],
        "preload": int(result["preload"]),
        "workers": result["server_workers"],
        "shards": result["server_shards"],
    }
    for field, expected in expected_integers.items():
        if not re.fullmatch(r"0|[1-9][0-9]*", row[field]) or int(row[field]) != expected:
            raise ValueError(f"{csv_path.name} {field} does not match JSON result")
    expected_numbers = {
        "seconds": result["seconds"],
        "ops_per_second": result["operations_per_second"],
        "p50_us": result["latency_us"]["p50"],
        "p95_us": result["latency_us"]["p95"],
        "p99_us": result["latency_us"]["p99"],
        "max_us": result["latency_us"]["max"],
        "duration_s": result["duration_bound_s"],
        "read_ratio": result["read_ratio"],
    }
    for field, expected in expected_numbers.items():
        try:
            actual = float(row[field])
        except ValueError as error:
            raise ValueError(f"{csv_path.name} {field} is not numeric") from error
        if not math.isfinite(actual) or not math.isclose(
            actual, float(expected), rel_tol=1e-5
        ):
            raise ValueError(f"{csv_path.name} {field} does not match JSON result")


def publish_summary(output_path: Path, grouped: dict[tuple[str, str], list[dict]]) -> None:
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", newline="", encoding="utf-8", dir=output_path.parent,
            prefix=".summary.", suffix=".tmp", delete=False,
        ) as output:
            temporary_path = Path(output.name)
            writer = csv.writer(output)
            writer.writerow([
                "run_id", "experiment", "variant", "valid_trials", "ops_per_second_min",
                "ops_per_second_median", "ops_per_second_max", "p99_us_median", "p99_us_max",
                "total_errors", "total_connection_errors",
            ])
            for (experiment, variant), results in sorted(grouped.items()):
                rates = [float(item["operations_per_second"]) for item in results]
                p99s = [float(item["latency_us"]["p99"]) for item in results]
                writer.writerow([
                    results[0]["run_id"], experiment, variant, len(results), min(rates),
                    statistics.median(rates), max(rates), statistics.median(p99s), max(p99s),
                    sum(int(item["errors"]) for item in results),
                    sum(int(item["connection_errors"]) for item in results),
                ])
            output.flush()
            os.fsync(output.fileno())
        os.link(temporary_path, output_path)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: summarize-benchmark.py RUN_DIRECTORY", file=sys.stderr)
        return 2
    run_directory = Path(sys.argv[1])
    manifest_path = run_directory / "manifest.csv"
    if manifest_path.is_symlink():
        print(f"manifest must not be a symbolic link: {manifest_path}", file=sys.stderr)
        return 1
    if not manifest_path.is_file():
        print(f"missing manifest: {manifest_path}", file=sys.stderr)
        return 1
    if manifest_path.stat().st_size > MAX_MANIFEST_EVIDENCE_BYTES:
        print("manifest exceeds the evidence size limit", file=sys.stderr)
        return 1

    try:
        with manifest_path.open(newline="", encoding="utf-8") as manifest:
            reader = csv.DictReader(manifest)
            fields = reader.fieldnames or []
            if len(fields) != len(set(fields)):
                raise ValueError("manifest contains duplicate column names")
            missing_fields = sorted(REQUIRED_MANIFEST_FIELDS - set(fields))
            if missing_fields:
                raise ValueError(
                    f"manifest is missing required columns: {', '.join(missing_fields)}"
                )
            present_production_fields = PRODUCTION_MANIFEST_FIELDS.intersection(fields)
            missing_production_fields = sorted(
                PRODUCTION_MANIFEST_FIELDS - set(fields)
            )
            if present_production_fields and missing_production_fields:
                raise ValueError(
                    "manifest is missing production columns: "
                    + ", ".join(missing_production_fields)
                )
            allowed_fields = REQUIRED_MANIFEST_FIELDS | (
                PRODUCTION_MANIFEST_FIELDS if present_production_fields else set()
            )
            unknown_fields = sorted(set(fields) - allowed_fields)
            if unknown_fields:
                raise ValueError(
                    "manifest contains unknown columns: " + ", ".join(unknown_fields)
                )
            rows = list(reader)
            production_manifest = bool(present_production_fields)
        if any(None in row for row in rows):
            raise ValueError("manifest row contains more fields than its header")
    except (csv.Error, OSError, ValueError) as error:
        print(f"invalid benchmark manifest: {error}", file=sys.stderr)
        return 1

    grouped: dict[tuple[str, str], list[dict]] = {}
    seen_trials: set[tuple[str, str, str, str]] = set()
    seen_output_prefixes: set[str] = set()
    run_id: str | None = None
    for row_number, row in enumerate(rows, start=1):
        try:
            status = row["status"]
            if status not in {"valid", "invalid"}:
                raise ValueError(f"manifest contains unknown status {status!r}")
            for field in ("experiment", "variant"):
                value = row[field]
                if not value.strip() or "\n" in value or "\r" in value:
                    raise ValueError(
                        f"manifest trial has an invalid {field}"
                    )
            if production_manifest:
                reason = row["reason"]
                if status == "valid" and reason != "none":
                    raise ValueError("manifest valid trial reason must be 'none'")
                if status == "invalid" and not re.fullmatch(
                    r"server-not-ready|benchmark-exit-[1-9][0-9]*", reason
                ):
                    raise ValueError(
                        f"manifest invalid trial has unsupported reason {reason!r}"
                    )
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", row["run_id"]):
                raise ValueError(
                    f"manifest run_id {row['run_id']!r} is not a safe identifier"
                )
            if run_id is None:
                run_id = row["run_id"]
            elif row["run_id"] != run_id:
                raise ValueError(
                    f"manifest mixes run ids {run_id!r} and {row['run_id']!r}"
                )
            if not re.fullmatch(r"[1-9][0-9]*", row["trial"]):
                raise ValueError(
                    "manifest trial must be a positive integer in canonical decimal form"
                )
            if not re.fullmatch(r"0|[1-9][0-9]*", row["seed"]):
                raise ValueError(
                    "manifest seed must be a nonnegative integer in canonical decimal form"
                )
            if "case_order" in row:
                if not re.fullmatch(r"[1-9][0-9]*", row["case_order"]):
                    raise ValueError(
                        "manifest case_order must be a positive integer in canonical decimal form"
                    )
                case_order = int(row["case_order"])
                if case_order != row_number:
                    raise ValueError(
                        "manifest case_order does not match row order"
                    )
            identity = (
                row["run_id"], row["experiment"], row["variant"], row["trial"]
            )
            if identity in seen_trials:
                raise ValueError(f"manifest contains duplicate trial {identity!r}")
            seen_trials.add(identity)
            output_prefix = row["output_prefix"]
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", output_prefix):
                raise ValueError(
                    f"manifest output prefix {output_prefix!r} is not a safe filename"
                )
            if output_prefix in seen_output_prefixes:
                raise ValueError(
                    f"manifest contains duplicate output prefix {output_prefix!r}"
                )
            seen_output_prefixes.add(output_prefix)
            resource_file = row.get("resource_file")
            if resource_file is not None and not re.fullmatch(
                r"[A-Za-z0-9][A-Za-z0-9._-]*", resource_file
            ):
                raise ValueError(
                    f"manifest resource file {resource_file!r} is not a safe filename"
                )
            if (
                production_manifest
                and resource_file != f"{output_prefix}-server-resource.txt"
            ):
                raise ValueError(
                    "manifest resource file does not match its output prefix"
                )
            if status == "invalid":
                continue
            resource_timestamp: dt.datetime | None = None
            if resource_file is not None:
                resource_path = run_directory / resource_file
                if resource_path.is_symlink():
                    raise ValueError(f"{resource_path.name} must not be a symbolic link")
                if not resource_path.is_file():
                    raise ValueError(f"resource evidence is missing: {resource_path.name}")
                resource_timestamp = validate_resource_evidence(resource_path)
            result_path = run_directory / (output_prefix + ".json")
            if result_path.is_symlink():
                raise ValueError(f"{result_path.name} must not be a symbolic link")
            result = json.loads(
                result_path.read_text(encoding="utf-8"),
                object_pairs_hook=unique_json_object,
            )
            if not isinstance(result, dict):
                raise ValueError(f"{result_path.name} must contain a JSON object")
            validate_result_metadata(result_path, row, result, production_manifest)
            if production_manifest:
                result_timestamp = dt.datetime.strptime(
                    result["timestamp_utc"], "%Y-%m-%dT%H:%M:%SZ"
                )
                if resource_timestamp is None or resource_timestamp < result_timestamp:
                    raise ValueError(
                        f"{resource_path.name} predates benchmark result"
                    )
                if resource_timestamp > result_timestamp + MAX_RESOURCE_CAPTURE_DELAY:
                    raise ValueError(
                        f"{resource_path.name} was captured too long after benchmark result"
                    )
                validate_latency_evidence(run_directory, output_prefix, result)
                validate_csv_evidence(run_directory, output_prefix, result)
        except (OSError, KeyError, TypeError, ValueError) as error:
            print(f"invalid benchmark result: {error}", file=sys.stderr)
            return 1
        grouped.setdefault((row["experiment"], row["variant"]), []).append(result)

    if not grouped:
        print("benchmark manifest contains no valid trials", file=sys.stderr)
        return 1

    output_path = run_directory / "summary.csv"
    try:
        publish_summary(output_path, grouped)
    except OSError as error:
        print(f"failed to publish benchmark summary: {error}", file=sys.stderr)
        return 1
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
