#!/usr/bin/env python3
"""Validate the complete CSV payload produced by the M3 contention benchmark."""

import argparse
import csv
import math
import pathlib
import re
import stat
import sys


HEADER = [
    "workload",
    "shards",
    "threads",
    "keys",
    "operations",
    "repetition",
    "seconds",
    "ops_per_second",
    "checksum",
]
METADATA_FIELDS = {
    "date_utc",
    "git_commit",
    "working_tree_dirty",
    "benchmark_binary_sha256",
    "benchmark_runner_sha256",
    "output_validator_sha256",
    "compiler",
    "compile_command",
    "os",
    "architecture",
    "cpu",
    "logical_cores",
    "ram",
    "storage_medium",
    "durability_mode",
    "dataset_size",
    "value_size",
    "connection_count",
    "worker_count",
    "shard_counts",
    "repetitions",
    "warmup",
}
WORKLOADS = {"same-key", "distributed-key"}
MAX_UINT64 = (1 << 64) - 1
MAX_EVIDENCE_BYTES = 1 << 20


def positive_integer(value: str, field: str) -> int:
    if re.fullmatch(r"[1-9][0-9]*", value) is None:
        raise ValueError(f"{field} must be a canonical positive integer")
    parsed = int(value)
    if parsed > MAX_UINT64:
        raise ValueError(f"{field} exceeds the unsigned 64-bit range")
    return parsed


def nonnegative_integer(value: str, field: str) -> int:
    if re.fullmatch(r"0|[1-9][0-9]*", value) is None:
        raise ValueError(f"{field} must be a canonical nonnegative integer")
    parsed = int(value)
    if parsed > MAX_UINT64:
        raise ValueError(f"{field} exceeds the unsigned 64-bit range")
    return parsed


def positive_decimal(value: str, field: str) -> float:
    if re.fullmatch(r"(?:0|[1-9][0-9]*)(?:\.[0-9]+)?", value) is None:
        raise ValueError(f"{field} must be a canonical positive decimal")
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0:
        raise ValueError(f"{field} must be a finite positive decimal")
    return parsed


def shard_counts(value: str) -> set[int]:
    parsed = [positive_integer(item, "shards") for item in value.split(",")]
    if len(set(parsed)) != len(parsed):
        raise ValueError("shards must not contain duplicates")
    return set(parsed)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=pathlib.Path)
    parser.add_argument("--threads", required=True)
    parser.add_argument("--operations-per-thread", required=True)
    parser.add_argument("--keys", required=True)
    parser.add_argument("--repetitions", required=True)
    parser.add_argument("--shards", required=True)
    arguments = parser.parse_args()

    for name in ("threads", "operations_per_thread", "keys", "repetitions"):
        field = name.replace("_", "-")
        setattr(arguments, name, positive_integer(getattr(arguments, name), field))

    expected_shards = shard_counts(arguments.shards)
    expected_operations = arguments.threads * arguments.operations_per_thread
    if expected_operations > MAX_UINT64:
        raise ValueError("requested operation count exceeds the unsigned 64-bit range")
    expected_run_count = len(WORKLOADS) * len(expected_shards) * arguments.repetitions

    evidence_status = arguments.path.lstat()
    if stat.S_ISLNK(evidence_status.st_mode) or not stat.S_ISREG(evidence_status.st_mode):
        raise ValueError("benchmark output must be a regular file, not a symlink or special file")
    if evidence_status.st_size > MAX_EVIDENCE_BYTES:
        raise ValueError(f"benchmark output exceeds {MAX_EVIDENCE_BYTES} bytes")

    with arguments.path.open(newline="") as stream:
        metadata = {}
        csv_lines = []
        csv_started = False
        for line in stream:
            if line.startswith("# "):
                if csv_started:
                    raise ValueError("benchmark metadata must precede the CSV payload")
                key, separator, value = line[2:].rstrip("\r\n").partition(",")
                if not separator or not key or not value:
                    raise ValueError("benchmark output has malformed metadata")
                if key not in METADATA_FIELDS:
                    raise ValueError(f"benchmark output has unknown metadata field: {key}")
                if key in metadata:
                    raise ValueError(f"benchmark output duplicates metadata field: {key}")
                metadata[key] = value
                continue
            csv_started = True
            csv_lines.append(line)

        missing_metadata = METADATA_FIELDS - metadata.keys()
        if missing_metadata:
            missing = ", ".join(sorted(missing_metadata))
            raise ValueError(f"benchmark output is missing metadata fields: {missing}")

        expected_metadata = {
            "dataset_size": f"{arguments.keys} keys",
            "worker_count": f"{arguments.threads} benchmark threads",
            "shard_counts": arguments.shards,
            "repetitions": str(arguments.repetitions),
        }
        for key, expected_value in expected_metadata.items():
            if metadata[key] != expected_value:
                raise ValueError(f"benchmark metadata {key} does not match the requested configuration")

        reader = csv.reader(csv_lines)
        try:
            header = next(reader)
        except StopIteration:
            raise ValueError("benchmark output has no CSV header")
        if header != HEADER:
            raise ValueError("benchmark output has an unexpected CSV header")

        observed_runs = set()
        for line_number, row in enumerate(reader, start=2):
            if len(row) != len(HEADER):
                raise ValueError(f"benchmark row {line_number} has {len(row)} fields; expected 9")
            record = dict(zip(HEADER, row))
            workload = record["workload"]
            if workload not in WORKLOADS:
                raise ValueError(f"benchmark row {line_number} has an unknown workload")
            shards = positive_integer(record["shards"], "shards")
            threads = positive_integer(record["threads"], "threads")
            keys = positive_integer(record["keys"], "keys")
            operations = positive_integer(record["operations"], "operations")
            repetition = positive_integer(record["repetition"], "repetition")
            nonnegative_integer(record["checksum"], "checksum")
            seconds = positive_decimal(record["seconds"], "seconds")
            throughput = positive_decimal(record["ops_per_second"], "ops_per_second")
            expected_throughput = operations / seconds
            if not math.isclose(throughput, expected_throughput, rel_tol=1e-4):
                raise ValueError(
                    f"benchmark row {line_number} has throughput inconsistent with operations/time"
                )
            if shards not in expected_shards:
                raise ValueError(f"benchmark row {line_number} has an unexpected shard count")
            if threads != arguments.threads or keys != arguments.keys:
                raise ValueError(f"benchmark row {line_number} does not match the requested configuration")
            if operations != expected_operations:
                raise ValueError(f"benchmark row {line_number} has an unexpected operation count")
            if repetition > arguments.repetitions:
                raise ValueError(f"benchmark row {line_number} has an unexpected repetition")
            run = (workload, shards, repetition)
            if run in observed_runs:
                raise ValueError(f"benchmark row {line_number} duplicates a workload run")
            observed_runs.add(run)

    if len(observed_runs) != expected_run_count:
        missing = expected_run_count - len(observed_runs)
        raise ValueError(f"benchmark output has incomplete run coverage ({missing} missing)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"invalid M3 contention output: {error}", file=sys.stderr)
        raise SystemExit(1)
