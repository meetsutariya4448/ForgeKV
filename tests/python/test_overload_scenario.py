#!/usr/bin/env python3
"""Focused protocol-validation tests for the overload scenario probe."""

from __future__ import annotations

import importlib.util
import pathlib
import struct
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "run-overload-scenario.py"
SPEC = importlib.util.spec_from_file_location("overload_scenario", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
SCENARIO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCENARIO)


def ping_response(request_id: int) -> tuple[bytes, bytes]:
    value = b"PONG"
    header = bytearray(SCENARIO.HEADER_SIZE)
    header[:4] = b"FKVP"
    struct.pack_into(">HHBBHHHQII", header, 4, 1, SCENARIO.HEADER_SIZE, 2, 7, 0, 0,
                     0, request_id, 0, len(value))
    struct.pack_into(">I", header, 32, SCENARIO.crc32c(header[:32]))
    struct.pack_into(">I", header, 36, SCENARIO.crc32c(value))
    return bytes(header), value


class PingValidationTest(unittest.TestCase):
    def test_accepts_valid_ping_response(self) -> None:
        header, value = ping_response(42)
        self.assertTrue(SCENARIO.valid_ping_header(header, 42))
        self.assertTrue(SCENARIO.valid_ping_payload(header, value))

    def test_rejects_corrupt_header_checksum(self) -> None:
        header, _ = ping_response(42)
        corrupted = bytearray(header)
        corrupted[32] ^= 0x01
        self.assertFalse(SCENARIO.valid_ping_header(bytes(corrupted), 42))

    def test_rejects_unsupported_flags_and_payload_sizes(self) -> None:
        header, _ = ping_response(42)
        for offset in (12, 28):
            corrupted = bytearray(header)
            corrupted[offset + 1] = 1
            struct.pack_into(">I", corrupted, 32, SCENARIO.crc32c(corrupted[:32]))
            self.assertFalse(SCENARIO.valid_ping_header(bytes(corrupted), 42))

    def test_rejects_corrupt_payload_checksum(self) -> None:
        header, value = ping_response(42)
        corrupted = bytearray(header)
        corrupted[39] ^= 0x01
        self.assertFalse(SCENARIO.valid_ping_payload(bytes(corrupted), value))


if __name__ == "__main__":
    unittest.main()
