#!/usr/bin/env python3
"""Summarize immutable ForgeKV benchmark trial JSON without third-party packages."""

import csv
import json
import math
import re
import statistics
import sys
from pathlib import Path


def validate_result_metadata(result_path: Path, row: dict[str, str], result: dict) -> None:
    expected = {
        "run_id": row["run_id"],
        "experiment": row["experiment"],
        "variant": row["variant"],
        "repetition": int(row["trial"]),
        "seed": int(row["seed"]),
    }
    for field, expected_value in expected.items():
        actual = result.get(field)
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

    for field in ("errors", "connection_errors"):
        value = result.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"{result_path.name} {field} must be a nonnegative integer")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: summarize-benchmark.py RUN_DIRECTORY", file=sys.stderr)
        return 2
    run_directory = Path(sys.argv[1])
    manifest_path = run_directory / "manifest.csv"
    if not manifest_path.is_file():
        print(f"missing manifest: {manifest_path}", file=sys.stderr)
        return 1

    rows = list(csv.DictReader(manifest_path.open(newline="", encoding="utf-8")))
    grouped: dict[tuple[str, str], list[dict]] = {}
    seen_trials: set[tuple[str, str, str, str, str]] = set()
    run_id: str | None = None
    for row in rows:
        if row["status"] != "valid":
            continue
        try:
            if run_id is None:
                run_id = row["run_id"]
            elif row["run_id"] != run_id:
                raise ValueError(
                    f"manifest mixes run ids {run_id!r} and {row['run_id']!r}"
                )
            identity = (
                row["run_id"], row["experiment"], row["variant"], row["trial"], row["seed"]
            )
            if identity in seen_trials:
                raise ValueError(f"manifest contains duplicate valid trial {identity!r}")
            seen_trials.add(identity)
            output_prefix = row["output_prefix"]
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", output_prefix):
                raise ValueError(
                    f"manifest output prefix {output_prefix!r} is not a safe filename"
                )
            result_path = run_directory / (output_prefix + ".json")
            result = json.loads(result_path.read_text(encoding="utf-8"))
            if not isinstance(result, dict):
                raise ValueError(f"{result_path.name} must contain a JSON object")
            validate_result_metadata(result_path, row, result)
        except (OSError, KeyError, TypeError, ValueError) as error:
            print(f"invalid benchmark result: {error}", file=sys.stderr)
            return 1
        grouped.setdefault((row["experiment"], row["variant"]), []).append(result)

    if not grouped:
        print("benchmark manifest contains no valid trials", file=sys.stderr)
        return 1

    output_path = run_directory / "summary.csv"
    with output_path.open("x", newline="", encoding="utf-8") as output:
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
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
