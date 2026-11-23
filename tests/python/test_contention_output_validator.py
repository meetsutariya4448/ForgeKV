#!/usr/bin/env python3
"""Regression tests for the M3 contention output validator."""

import csv
import pathlib
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
VALIDATOR = REPOSITORY_ROOT / "scripts" / "validate-m3-contention-output.py"
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


class ContentionOutputValidatorTest(unittest.TestCase):
    def run_validator(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            evidence = pathlib.Path(directory) / "contention.csv"
            with evidence.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(HEADER)
                writer.writerows(rows)
            return subprocess.run(
                [
                    sys.executable,
                    str(VALIDATOR),
                    str(evidence),
                    "--threads",
                    "1",
                    "--operations-per-thread",
                    "1",
                    "--keys",
                    "1",
                    "--repetitions",
                    "1",
                    "--shards",
                    "1",
                ],
                capture_output=True,
                check=False,
                text=True,
            )

    def test_accepts_complete_canonical_output(self):
        result = self.run_validator(
            [
                ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
                ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
            ]
        )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_noncanonical_integer_fields(self):
        for field_index, value in ((1, "01"), (8, "+1")):
            with self.subTest(field=HEADER[field_index], value=value):
                rows = [
                    ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
                    ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
                ]
                rows[0][field_index] = value

                result = self.run_validator(rows)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("canonical", result.stderr)


if __name__ == "__main__":
    unittest.main()
