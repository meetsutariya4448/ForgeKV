#!/usr/bin/env python3
"""Exercise ForgeKV connection saturation with partial-frame slow clients on Linux."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time


POLY = 0x82F63B78
HEADER_SIZE = 40


def crc32c(data: bytes) -> int:
    value = 0xFFFFFFFF
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (POLY if value & 1 else 0)
    return (~value) & 0xFFFFFFFF


def ping_frame(request_id: int) -> bytes:
    header = bytearray(HEADER_SIZE)
    header[0:4] = b"FKVP"
    struct.pack_into(">HHBBHHHQII", header, 4, 1, HEADER_SIZE, 1, 7, 0, 0, 0,
                     request_id, 0, 0)
    struct.pack_into(">I", header, 32, crc32c(header[:32]))
    struct.pack_into(">I", header, 36, crc32c(b""))
    return bytes(header)


def recv_exact(connection: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = connection.recv(size - len(chunks))
        if not chunk:
            break
        chunks.extend(chunk)
    return bytes(chunks)


def ping(host: str, port: int, request_id: int, timeout: float) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout) as connection:
            connection.settimeout(timeout)
            connection.sendall(ping_frame(request_id))
            header = recv_exact(connection, HEADER_SIZE)
            if len(header) != HEADER_SIZE or header[:4] != b"FKVP":
                return False
            kind, opcode, status = header[8], header[9], struct.unpack_from(">H", header, 10)[0]
            request = struct.unpack_from(">Q", header, 16)[0]
            value_size = struct.unpack_from(">I", header, 28)[0]
            value = recv_exact(connection, value_size)
            return (kind, opcode, status, request, value) == (2, 7, 0, request_id, b"PONG")
    except (ConnectionError, OSError, socket.timeout):
        return False


def resources(pid: int) -> dict[str, int]:
    status: dict[str, int] = {}
    for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
        name, _, value = line.partition(":")
        if name in {"VmRSS", "VmHWM", "VmPeak", "Threads"}:
            status[name] = int(value.strip().split()[0])
    status["open_fds"] = len(list(pathlib.Path(f"/proc/{pid}/fd").iterdir()))
    return status


def wait_until(predicate, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.02)
    return predicate()


def reserve_port(host: str) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((host, 0))
        return int(probe.getsockname()[1])


def git_metadata(root: pathlib.Path) -> tuple[str, bool]:
    try:
        sha = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True,
            stderr=subprocess.DEVNULL).strip()
        dirty = bool(subprocess.check_output(
            ["git", "-C", str(root), "status", "--porcelain"], text=True,
            stderr=subprocess.DEVNULL).strip())
        return sha, dirty
    except (OSError, subprocess.CalledProcessError):
        return "unknown", True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--max-connections", type=int, default=32)
    parser.add_argument("--excess-connections", type=int, default=16)
    parser.add_argument("--io-timeout-ms", type=int, default=2000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if os.uname().sysname != "Linux":
        raise SystemExit("overload scenario requires Linux /proc")
    if args.max_connections <= 0 or args.excess_connections <= 0 or args.io_timeout_ms <= 0:
        raise SystemExit("connection counts and timeout must be positive")
    if args.output.exists():
        raise SystemExit(f"refusing to overwrite {args.output}")
    if not args.server.is_file() or not os.access(args.server, os.X_OK):
        raise SystemExit(f"server is not executable: {args.server}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    log_path = args.output.with_suffix(".server.log")
    data_directory = pathlib.Path(tempfile.mkdtemp(prefix="forgekv-overload-"))
    port = reserve_port(args.host)
    command = [
        str(args.server), "--host", args.host, "--port", str(port), "--data", str(data_directory),
        "--workers", "4", "--queue-capacity", "16", "--max-connections",
        str(args.max_connections), "--io-timeout-ms", str(args.io_timeout_ms),
        "--durability", "none", "--no-background-compaction",
    ]
    server_log = log_path.open("xb")
    process = subprocess.Popen(command, stdout=server_log, stderr=subprocess.STDOUT)
    slow_clients: list[socket.socket] = []
    try:
        if not wait_until(lambda: ping(args.host, port, 1, 0.1), 5):
            raise RuntimeError("server did not become ready")
        time.sleep(0.2)
        baseline = resources(process.pid)

        admission_deadline = time.monotonic() + 15
        while len(slow_clients) < args.max_connections:
            if time.monotonic() >= admission_deadline:
                raise RuntimeError(
                    f"only {len(slow_clients)} slow clients were admitted before timeout")
            connection: socket.socket | None = None
            try:
                connection = socket.create_connection((args.host, port), timeout=1)
                connection.settimeout(0.5)
                connection.sendall(b"F")  # Valid frame prefix, deliberately incomplete.
                target_threads = baseline["Threads"] + len(slow_clients) + 1
                if wait_until(lambda: resources(process.pid)["Threads"] >= target_threads, 0.5):
                    slow_clients.append(connection)
                    connection = None
                else:
                    time.sleep(0.02)
            except (ConnectionError, OSError, socket.timeout):
                time.sleep(0.02)
            finally:
                if connection is not None:
                    connection.close()

        expected_threads = baseline["Threads"] + args.max_connections
        wait_until(lambda: resources(process.pid)["Threads"] >= expected_threads, 2)
        saturated = resources(process.pid)

        rejected = 0
        unexpectedly_served = 0
        for request_id in range(100, 100 + args.excess_connections):
            if ping(args.host, port, request_id, 0.5):
                unexpectedly_served += 1
            else:
                rejected += 1

        during_excess = resources(process.pid)
        for connection in slow_clients:
            connection.close()
        slow_clients.clear()

        recovered = wait_until(lambda: ping(args.host, port, 10_000, 0.5), 5)
        wait_until(lambda: resources(process.pid)["Threads"] <= baseline["Threads"] + 2, 5)
        after_recovery = resources(process.pid)

        thread_bound = baseline["Threads"] + args.max_connections + 2
        fd_bound = baseline["open_fds"] + args.max_connections + 2
        checks = {
            "all_excess_connections_rejected": rejected == args.excess_connections,
            "no_excess_request_served": unexpectedly_served == 0,
            "threads_bounded": saturated["Threads"] <= thread_bound,
            "file_descriptors_bounded": saturated["open_fds"] <= fd_bound,
            "server_recovers_after_slow_clients_close": recovered,
            "threads_return_near_baseline": after_recovery["Threads"] <= baseline["Threads"] + 2,
            "file_descriptors_return_near_baseline":
                after_recovery["open_fds"] <= baseline["open_fds"] + 2,
        }
        project_root = pathlib.Path(__file__).resolve().parents[1]
        git_sha, working_tree_dirty = git_metadata(project_root)
        system = os.uname()
        result = {
            "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "git_sha": git_sha,
            "working_tree_dirty": working_tree_dirty,
            "system": f"{system.sysname} {system.release} {system.machine}",
            "scenario": "partial-frame slow clients fill the connection limit; excess PINGs are rejected",
            "server": str(args.server),
            "server_pid": process.pid,
            "host": args.host,
            "port": port,
            "max_connections": args.max_connections,
            "excess_connections": args.excess_connections,
            "io_timeout_ms": args.io_timeout_ms,
            "resource_bounds": {"threads": thread_bound, "open_fds": fd_bound},
            "resources": {
                "baseline": baseline,
                "saturated": saturated,
                "after_excess_attempts": during_excess,
                "after_recovery": after_recovery,
            },
            "excess_results": {"rejected": rejected, "unexpectedly_served": unexpectedly_served},
            "checks": checks,
            "passed": all(checks.values()),
        }
        args.output.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        return 0 if result["passed"] else 1
    finally:
        for connection in slow_clients:
            connection.close()
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        server_log.close()
        shutil.rmtree(data_directory, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
