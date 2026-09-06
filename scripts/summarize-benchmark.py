#!/usr/bin/env python3
"""Summarize immutable ForgeKV benchmark trial JSON without third-party packages."""

import csv
import json
import statistics
import sys
from pathlib import Path


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
    for row in rows:
        if row["status"] != "valid":
            continue
        result_path = run_directory / (row["output_prefix"] + ".json")
        result = json.loads(result_path.read_text(encoding="utf-8"))
        grouped.setdefault((row["experiment"], row["variant"]), []).append(result)

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
