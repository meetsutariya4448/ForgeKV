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
DEFAULT_ARGUMENTS = [
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
]
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
    def run_validator(self, rows, arguments=None):
        with tempfile.TemporaryDirectory() as directory:
            evidence = pathlib.Path(directory) / "contention.csv"
            with evidence.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(HEADER)
                writer.writerows(rows)
            if arguments is None:
                arguments = DEFAULT_ARGUMENTS
            return subprocess.run(
                [sys.executable, str(VALIDATOR), str(evidence), *arguments],
                capture_output=True,
                check=False,
                text=True,
                timeout=5,
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

    def test_rejects_nonpositive_requested_dimensions(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]
        options = ("--threads", "--operations-per-thread", "--keys", "--repetitions")
        for option in options:
            with self.subTest(option=option):
                arguments = [
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
                ]
                arguments[arguments.index(option) + 1] = "0"

                result = self.run_validator(rows, arguments)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("positive integer", result.stderr)

    def test_rejects_noncanonical_requested_dimensions(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]
        for value in ("01", "+1", " 1"):
            with self.subTest(value=value):
                arguments = [
                    "--threads",
                    value,
                    "--operations-per-thread",
                    "1",
                    "--keys",
                    "1",
                    "--repetitions",
                    "1",
                    "--shards",
                    "1",
                ]

                result = self.run_validator(rows, arguments)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("canonical positive integer", result.stderr)

    def test_rejects_ambiguous_requested_shards(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]
        invalid_shards = (("1,1", "duplicates"), ("01", "canonical"), ("", "canonical"))
        for shards, expected_error in invalid_shards:
            with self.subTest(shards=shards):
                result = self.run_validator(
                    rows,
                    [
                        "--threads",
                        "1",
                        "--operations-per-thread",
                        "1",
                        "--keys",
                        "1",
                        "--repetitions",
                        "1",
                        "--shards",
                        shards,
                    ],
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_rejects_counters_outside_unsigned_64_bit_range(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "1", "0.1", "10", str(1 << 64)],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]

        result = self.run_validator(rows)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsigned 64-bit range", result.stderr)

    def test_rejects_requested_operation_count_overflow(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]

        result = self.run_validator(
            rows,
            [
                "--threads",
                str((1 << 64) - 1),
                "--operations-per-thread",
                "2",
                "--keys",
                "1",
                "--repetitions",
                "1",
                "--shards",
                "1",
            ],
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("operation count exceeds", result.stderr)

    def test_rejects_requested_dimensions_outside_unsigned_64_bit_range(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]

        result = self.run_validator(
            rows,
            [
                "--threads",
                "1",
                "--operations-per-thread",
                "1",
                "--keys",
                str(1 << 64),
                "--repetitions",
                "1",
                "--shards",
                "1",
            ],
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("keys exceeds the unsigned 64-bit range", result.stderr)

    def test_large_requested_run_counts_do_not_require_materializing_every_run(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "1", "0.1", "10", "0"],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]

        result = self.run_validator(
            rows,
            [
                "--threads",
                "1",
                "--operations-per-thread",
                "1",
                "--keys",
                "1",
                "--repetitions",
                "1000000",
                "--shards",
                "1",
            ],
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("1999998 missing", result.stderr)

    def test_rejects_repetitions_outside_requested_range(self):
        rows = [
            ["same-key", "1", "1", "1", "1", "2", "0.1", "10", "0"],
            ["distributed-key", "1", "1", "1", "1", "1", "0.1", "10", "1"],
        ]

        result = self.run_validator(rows)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unexpected repetition", result.stderr)

    def test_rejects_symlinked_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "target.csv"
            target.write_text(",".join(HEADER) + "\n")
            evidence = pathlib.Path(directory) / "contention.csv"
            evidence.symlink_to(target)

            result = subprocess.run(
                [sys.executable, str(VALIDATOR), str(evidence), *DEFAULT_ARGUMENTS],
                capture_output=True,
                check=False,
                text=True,
                timeout=5,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("regular file", result.stderr)

    def test_rejects_oversized_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = pathlib.Path(directory) / "contention.csv"
            evidence.write_bytes(b"x" * ((1 << 20) + 1))

            result = subprocess.run(
                [sys.executable, str(VALIDATOR), str(evidence), *DEFAULT_ARGUMENTS],
                capture_output=True,
                check=False,
                text=True,
                timeout=5,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exceeds 1048576 bytes", result.stderr)


if __name__ == "__main__":
    unittest.main()
