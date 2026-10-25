#!/usr/bin/env python3
"""Validate the complete CSV payload produced by the M3 contention benchmark."""

import argparse
import csv
import math
import pathlib
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
WORKLOADS = {"same-key", "distributed-key"}


def positive_integer(value: str, field: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise ValueError(f"{field} must be an integer") from error
    if parsed <= 0:
        raise ValueError(f"{field} must be positive")
    return parsed


def nonnegative_integer(value: str, field: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise ValueError(f"{field} must be an integer") from error
    if parsed < 0:
        raise ValueError(f"{field} must not be negative")
    return parsed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=pathlib.Path)
    parser.add_argument("--threads", type=int, required=True)
    parser.add_argument("--operations-per-thread", type=int, required=True)
    parser.add_argument("--keys", type=int, required=True)
    parser.add_argument("--repetitions", type=int, required=True)
    parser.add_argument("--shards", required=True)
    arguments = parser.parse_args()

    expected_shards = {int(value) for value in arguments.shards.split(",")}
    expected_operations = arguments.threads * arguments.operations_per_thread
    expected_runs = {
        (workload, shards, repetition)
        for workload in WORKLOADS
        for shards in expected_shards
        for repetition in range(1, arguments.repetitions + 1)
    }

    with arguments.path.open(newline="") as stream:
        reader = csv.reader(line for line in stream if not line.startswith("#"))
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
            try:
                seconds = float(record["seconds"])
                throughput = float(record["ops_per_second"])
            except ValueError as error:
                raise ValueError(f"benchmark row {line_number} has a nonnumeric measurement") from error
            if not math.isfinite(seconds) or seconds <= 0:
                raise ValueError(f"benchmark row {line_number} has invalid elapsed time")
            if not math.isfinite(throughput) or throughput <= 0:
                raise ValueError(f"benchmark row {line_number} has invalid throughput")
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
            run = (workload, shards, repetition)
            if run in observed_runs:
                raise ValueError(f"benchmark row {line_number} duplicates a workload run")
            observed_runs.add(run)

    if observed_runs != expected_runs:
        missing = len(expected_runs - observed_runs)
        extra = len(observed_runs - expected_runs)
        raise ValueError(f"benchmark output has incomplete run coverage ({missing} missing, {extra} extra)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"invalid M3 contention output: {error}", file=sys.stderr)
        raise SystemExit(1)
