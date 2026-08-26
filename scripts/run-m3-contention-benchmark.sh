#!/bin/sh
set -eu

FORGEKV_BENCH_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
FORGEKV_BENCH_THREADS=${FORGEKV_BENCH_THREADS:-4}
FORGEKV_BENCH_OPERATIONS=${FORGEKV_BENCH_OPERATIONS:-100000}
FORGEKV_BENCH_KEYS=${FORGEKV_BENCH_KEYS:-4096}
FORGEKV_BENCH_REPETITIONS=${FORGEKV_BENCH_REPETITIONS:-3}
FORGEKV_BENCH_SHARDS=${FORGEKV_BENCH_SHARDS:-1,4,16,64,256}
FORGEKV_BENCH_SHA=$(git -C "$FORGEKV_BENCH_ROOT" rev-parse HEAD)
FORGEKV_BENCH_TIMESTAMP=$(date -u +%Y%m%dT%H%M%SZ)
FORGEKV_BENCH_OUTPUT="$FORGEKV_BENCH_ROOT/bench/raw/m3-contention-${FORGEKV_BENCH_TIMESTAMP}-${FORGEKV_BENCH_SHA}.csv"

if [ ! -x "$FORGEKV_BENCH_ROOT/build/forgekv-bench" ]; then
    echo "build/forgekv-bench is missing; configure and build ForgeKV first" >&2
    exit 1
fi

FORGEKV_BENCH_OS=$(uname -srv)
FORGEKV_BENCH_ARCH=$(uname -m)
FORGEKV_BENCH_CPU=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)
if [ -z "$FORGEKV_BENCH_CPU" ] && [ -r /proc/cpuinfo ]; then
    FORGEKV_BENCH_CPU=$(awk -F: '/model name/ {sub(/^ /, "", $2); print $2; exit}' /proc/cpuinfo)
fi
FORGEKV_BENCH_CORES=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo unknown)
FORGEKV_BENCH_RAM=$(sysctl -n hw.memsize 2>/dev/null || true)
if [ -z "$FORGEKV_BENCH_RAM" ] && [ -r /proc/meminfo ]; then
    FORGEKV_BENCH_RAM=$(awk '/MemTotal/ {print $2 " kB"; exit}' /proc/meminfo)
fi
FORGEKV_BENCH_COMPILER=$(/usr/bin/c++ --version | sed -n '1p')
FORGEKV_BENCH_COMPILE_COMMAND=$(python3 -c '
import json, pathlib, sys
commands = json.loads(pathlib.Path(sys.argv[1]).read_text())
match = next(item for item in commands if item["file"].endswith("apps/forgekv-bench/main.cpp"))
print(match.get("command", " ".join(match["arguments"])))
' "$FORGEKV_BENCH_ROOT/build/compile_commands.json")

set -C
{
    echo "# date_utc,$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# git_commit,$FORGEKV_BENCH_SHA"
    echo "# compiler,$FORGEKV_BENCH_COMPILER"
    echo "# compile_command,$FORGEKV_BENCH_COMPILE_COMMAND"
    echo "# os,$FORGEKV_BENCH_OS"
    echo "# architecture,$FORGEKV_BENCH_ARCH"
    echo "# cpu,$FORGEKV_BENCH_CPU"
    echo "# logical_cores,$FORGEKV_BENCH_CORES"
    echo "# ram,$FORGEKV_BENCH_RAM"
    echo "# storage_medium,not applicable (in-memory index benchmark)"
    echo "# durability_mode,not applicable"
    echo "# dataset_size,$FORGEKV_BENCH_KEYS keys"
    echo "# value_size,not applicable"
    echo "# connection_count,not applicable"
    echo "# worker_count,$FORGEKV_BENCH_THREADS benchmark threads"
    echo "# shard_counts,$FORGEKV_BENCH_SHARDS"
    echo "# repetitions,$FORGEKV_BENCH_REPETITIONS"
    echo "# warmup,1000 operations per thread before each workload and shard count"
    "$FORGEKV_BENCH_ROOT/build/forgekv-bench" contention \
        --threads "$FORGEKV_BENCH_THREADS" \
        --operations-per-thread "$FORGEKV_BENCH_OPERATIONS" \
        --keys "$FORGEKV_BENCH_KEYS" \
        --repetitions "$FORGEKV_BENCH_REPETITIONS" \
        --shards "$FORGEKV_BENCH_SHARDS"
} > "$FORGEKV_BENCH_OUTPUT"

echo "$FORGEKV_BENCH_OUTPUT"
