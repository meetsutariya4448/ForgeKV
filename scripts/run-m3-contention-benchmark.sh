#!/bin/sh
set -eu

FORGEKV_BENCH_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
FORGEKV_BENCH_BUILD_DIR=${FORGEKV_BENCH_BUILD_DIR:-$FORGEKV_BENCH_ROOT/build}
FORGEKV_BENCH_OUTPUT_DIR=${FORGEKV_BENCH_OUTPUT_DIR:-$FORGEKV_BENCH_ROOT/bench/raw}
FORGEKV_BENCH_THREADS=${FORGEKV_BENCH_THREADS:-4}
FORGEKV_BENCH_OPERATIONS=${FORGEKV_BENCH_OPERATIONS:-100000}
FORGEKV_BENCH_KEYS=${FORGEKV_BENCH_KEYS:-4096}
FORGEKV_BENCH_REPETITIONS=${FORGEKV_BENCH_REPETITIONS:-3}
FORGEKV_BENCH_SHARDS=${FORGEKV_BENCH_SHARDS:-1,4,16,64,256}

validate_positive_integer() {
    name=$1
    value=$2
    case "$value" in
        ''|*[!0-9]*|0|0[0-9]*)
            echo "$name must be a canonical positive integer" >&2
            exit 2
            ;;
    esac
}
validate_positive_integer FORGEKV_BENCH_THREADS "$FORGEKV_BENCH_THREADS"
validate_positive_integer FORGEKV_BENCH_OPERATIONS "$FORGEKV_BENCH_OPERATIONS"
validate_positive_integer FORGEKV_BENCH_KEYS "$FORGEKV_BENCH_KEYS"
validate_positive_integer FORGEKV_BENCH_REPETITIONS "$FORGEKV_BENCH_REPETITIONS"
if ! python3 - "$FORGEKV_BENCH_SHARDS" <<'PY'
import re
import sys

text = sys.argv[1]
if not re.fullmatch(r"[1-9][0-9]*(,[1-9][0-9]*)*", text):
    print(
        "FORGEKV_BENCH_SHARDS must be a comma-separated list of canonical "
        "positive integers",
        file=sys.stderr,
    )
    raise SystemExit(2)
values = text.split(",")
if len(values) != len(set(values)):
    print("FORGEKV_BENCH_SHARDS must not contain duplicates", file=sys.stderr)
    raise SystemExit(2)
PY
then
    exit 2
fi

FORGEKV_BENCH_SHA=$(git -C "$FORGEKV_BENCH_ROOT" rev-parse HEAD)
case "$FORGEKV_BENCH_SHA" in
    ''|*[!0-9a-f]*) valid_git_sha=false ;;
    *) valid_git_sha=true ;;
esac
if [ "$valid_git_sha" != true ] || [ "${#FORGEKV_BENCH_SHA}" -ne 40 ]; then
    echo "M3 contention benchmark cannot identify a 40-character Git commit" >&2
    exit 1
fi
if [ -n "$(git -C "$FORGEKV_BENCH_ROOT" status --porcelain)" ]; then
    FORGEKV_BENCH_DIRTY=true
else
    FORGEKV_BENCH_DIRTY=false
fi
FORGEKV_BENCH_DATE_UTC=$(date -u +%Y-%m-%dT%H:%M:%SZ)
FORGEKV_BENCH_TIMESTAMP=$(printf '%s' "$FORGEKV_BENCH_DATE_UTC" | tr -d ':-')
FORGEKV_BENCH_OUTPUT="$FORGEKV_BENCH_OUTPUT_DIR/m3-contention-${FORGEKV_BENCH_TIMESTAMP}-${FORGEKV_BENCH_SHA}.csv"

if [ ! -x "$FORGEKV_BENCH_BUILD_DIR/forgekv-bench" ]; then
    echo "forgekv-bench is missing under $FORGEKV_BENCH_BUILD_DIR" >&2
    exit 1
fi
build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$FORGEKV_BENCH_BUILD_DIR/CMakeCache.txt")
if [ "$build_type" != "Release" ]; then
    echo "M3 contention benchmark requires a Release build; found ${build_type:-unknown}" >&2
    exit 1
fi
if [ ! -r "$FORGEKV_BENCH_BUILD_DIR/compile_commands.json" ]; then
    echo "M3 contention benchmark requires compile_commands.json under $FORGEKV_BENCH_BUILD_DIR" >&2
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
FORGEKV_BENCH_CXX=$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$FORGEKV_BENCH_BUILD_DIR/CMakeCache.txt")
if [ -z "$FORGEKV_BENCH_CXX" ] || [ ! -x "$FORGEKV_BENCH_CXX" ]; then
    echo "M3 contention benchmark cannot identify the configured C++ compiler" >&2
    exit 1
fi
FORGEKV_BENCH_COMPILER=$("$FORGEKV_BENCH_CXX" --version 2>/dev/null | sed -n '1p')
if [ -z "$FORGEKV_BENCH_COMPILER" ]; then
    echo "M3 contention benchmark cannot read the configured C++ compiler version" >&2
    exit 1
fi
FORGEKV_BENCH_BINARY_SHA256=$(python3 -c '
import hashlib, pathlib, sys
print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
' "$FORGEKV_BENCH_BUILD_DIR/forgekv-bench")
FORGEKV_BENCH_RUNNER_SHA256=$(python3 -c '
import hashlib, pathlib, sys
print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
' "$FORGEKV_BENCH_ROOT/scripts/run-m3-contention-benchmark.sh")
FORGEKV_BENCH_VALIDATOR_SHA256=$(python3 -c '
import hashlib, pathlib, sys
print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
' "$FORGEKV_BENCH_ROOT/scripts/validate-m3-contention-output.py")
FORGEKV_BENCH_COMPILE_COMMAND=$(python3 - "$FORGEKV_BENCH_BUILD_DIR/compile_commands.json" <<'PY'
import json
import pathlib
import shlex
import sys

path = pathlib.Path(sys.argv[1])
try:
    commands = json.loads(path.read_text())
except (OSError, UnicodeError, json.JSONDecodeError) as error:
    print(f"invalid compile_commands.json: {error}", file=sys.stderr)
    raise SystemExit(1)

if not isinstance(commands, list):
    print("invalid compile_commands.json: top-level value must be an array", file=sys.stderr)
    raise SystemExit(1)

matches = [
    item
    for item in commands
    if isinstance(item, dict)
    and isinstance(item.get("file"), str)
    and item["file"].endswith("apps/forgekv-bench/main.cpp")
]
if len(matches) != 1:
    print(
        "invalid compile_commands.json: expected exactly one forgekv-bench translation unit",
        file=sys.stderr,
    )
    raise SystemExit(1)

match = matches[0]
command = match.get("command")
arguments = match.get("arguments")
if isinstance(command, str) and command:
    print(command)
elif (
    isinstance(arguments, list)
    and arguments
    and all(isinstance(argument, str) for argument in arguments)
):
    print(shlex.join(arguments))
else:
    print(
        "invalid compile_commands.json: forgekv-bench entry has no usable command",
        file=sys.stderr,
    )
    raise SystemExit(1)
PY
)

if [ ! -d "$FORGEKV_BENCH_OUTPUT_DIR" ]; then
    echo "benchmark output directory does not exist: $FORGEKV_BENCH_OUTPUT_DIR" >&2
    exit 1
fi
temporary_output=$(mktemp "$FORGEKV_BENCH_OUTPUT_DIR/.m3-contention.XXXXXX")
cleanup() {
    if [ -n "$temporary_output" ]; then
        rm -f "$temporary_output"
    fi
}
trap cleanup EXIT INT TERM
{
    echo "# date_utc,$FORGEKV_BENCH_DATE_UTC"
    echo "# git_commit,$FORGEKV_BENCH_SHA"
    echo "# working_tree_dirty,$FORGEKV_BENCH_DIRTY"
    echo "# benchmark_binary_sha256,$FORGEKV_BENCH_BINARY_SHA256"
    echo "# benchmark_runner_sha256,$FORGEKV_BENCH_RUNNER_SHA256"
    echo "# output_validator_sha256,$FORGEKV_BENCH_VALIDATOR_SHA256"
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
    "$FORGEKV_BENCH_BUILD_DIR/forgekv-bench" contention \
        --threads "$FORGEKV_BENCH_THREADS" \
        --operations-per-thread "$FORGEKV_BENCH_OPERATIONS" \
        --keys "$FORGEKV_BENCH_KEYS" \
        --repetitions "$FORGEKV_BENCH_REPETITIONS" \
        --shards "$FORGEKV_BENCH_SHARDS"
} > "$temporary_output"

python3 "$FORGEKV_BENCH_ROOT/scripts/validate-m3-contention-output.py" \
    "$temporary_output" \
    --threads "$FORGEKV_BENCH_THREADS" \
    --operations-per-thread "$FORGEKV_BENCH_OPERATIONS" \
    --keys "$FORGEKV_BENCH_KEYS" \
    --repetitions "$FORGEKV_BENCH_REPETITIONS" \
    --shards "$FORGEKV_BENCH_SHARDS"

if ! ln "$temporary_output" "$FORGEKV_BENCH_OUTPUT"; then
    echo "refusing to replace existing benchmark evidence: $FORGEKV_BENCH_OUTPUT" >&2
    exit 1
fi
rm -f "$temporary_output"
temporary_output=""

echo "$FORGEKV_BENCH_OUTPUT"
