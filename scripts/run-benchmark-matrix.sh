#!/bin/sh
set -u

FORGEKV_BENCH_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mode=${1:-quick}
port=${FORGEKV_BENCH_PORT:-17411}
build_dir=${FORGEKV_BENCH_BUILD_DIR:-$FORGEKV_BENCH_ROOT/build-release}
base_seed=${FORGEKV_BENCH_SEED:-1}
storage_medium=${FORGEKV_BENCH_STORAGE_MEDIUM:-unspecified}

case "$mode" in
    quick)
        connections="1 10"
        workers="1 4"
        shards="1 16"
        values="16 1024"
        mixes="1.0 0.8 0.0"
        durability_modes="always periodic none"
        requests=2000
        default_trials=3
        ;;
    full)
        connections="1 10 50 100 250 500 1000"
        workers="1 2 4 8 16"
        shards="1 4 16 64 256"
        values="16 128 1024 16384"
        mixes="1.0 0.95 0.8 0.5 0.0"
        durability_modes="always periodic none"
        requests=100000
        default_trials=5
        ;;
    *)
        echo "usage: $0 [quick|full]" >&2
        exit 2
        ;;
esac
trials=${FORGEKV_BENCH_TRIALS:-$default_trials}
case "$trials" in
    ''|*[!0-9]*|0) echo "FORGEKV_BENCH_TRIALS must be a positive integer" >&2; exit 2 ;;
esac
if ! python3 - "$port" "$base_seed" "$trials" <<'PY'
import sys

port_text, seed_text, trials_text = sys.argv[1:]
if not port_text or any(character not in "0123456789" for character in port_text):
    print("FORGEKV_BENCH_PORT must be an integer from 1 to 65535", file=sys.stderr)
    raise SystemExit(2)
port = int(port_text)
if not 1 <= port <= 65535:
    print("FORGEKV_BENCH_PORT must be an integer from 1 to 65535", file=sys.stderr)
    raise SystemExit(2)
if not seed_text or any(character not in "0123456789" for character in seed_text):
    print("FORGEKV_BENCH_SEED must be a nonnegative integer", file=sys.stderr)
    raise SystemExit(2)
seed = int(seed_text)
trials = int(trials_text)
if seed + trials - 1 > 2**63 - 1:
    print("FORGEKV_BENCH_SEED and trial count exceed shell arithmetic range", file=sys.stderr)
    raise SystemExit(2)
PY
then
    exit 2
fi

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
git_sha=$(git -C "$FORGEKV_BENCH_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)
run_id=${FORGEKV_BENCH_RUN_ID:-matrix-${timestamp}-${git_sha}}
case "$run_id" in
    ''|.|..|*[!A-Za-z0-9._-]*)
        echo "FORGEKV_BENCH_RUN_ID must be a safe filename component" >&2
        exit 2
        ;;
esac

if [ ! -x "$build_dir/forgekv-server" ] || [ ! -x "$build_dir/forgekv-bench" ] ||
   [ ! -x "$build_dir/forgekv-cli" ]; then
    echo "Release binaries are missing under $build_dir" >&2
    exit 1
fi
build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$build_dir/CMakeCache.txt")
if [ "$build_type" != "Release" ]; then
    echo "benchmark matrix requires a Release build; found ${build_type:-unknown}" >&2
    exit 1
fi
run_dir="$FORGEKV_BENCH_ROOT/bench/raw/$run_id"
if [ -e "$run_dir" ]; then
    echo "refusing to overwrite existing benchmark run: $run_dir" >&2
    exit 1
fi
mkdir -p "$run_dir"
manifest="$run_dir/manifest.csv"
printf '%s\n' "run_id,case_order,experiment,variant,trial,seed,status,reason,output_prefix,resource_file" > "$manifest"
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/forgekv-matrix.XXXXXX")
server_pid=""
case_order=0

ram_description=$(sysctl -n hw.memsize 2>/dev/null || true)
if [ -n "$ram_description" ]; then
    ram_description="${ram_description} bytes"
elif [ -r /proc/meminfo ]; then
    ram_description=$(awk '/MemTotal/ {print $2 " kB"; exit}' /proc/meminfo)
else
    ram_description=unspecified
fi

stop_server() {
    if [ -n "$server_pid" ]; then
        kill -INT "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
    fi
}

cleanup() {
    stop_server
    rm -rf "$temporary_root"
}
trap cleanup EXIT INT TERM

capture_resources() {
    output=$1
    {
        echo "captured_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "pid=$server_pid"
        echo "system=$(uname -srvmo 2>/dev/null || uname -a)"
        if [ -r "/proc/$server_pid/status" ]; then
            sed -n '/^VmPeak:/p;/^VmSize:/p;/^VmHWM:/p;/^VmRSS:/p;/^Threads:/p' "/proc/$server_pid/status"
            if [ -r "/proc/$server_pid/io" ]; then
                cat "/proc/$server_pid/io"
            fi
            if [ -d "/proc/$server_pid/fd" ]; then
                echo "open_fds=$(find "/proc/$server_pid/fd" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l | tr -d ' ')"
            fi
        else
            ps -p "$server_pid" -o pid=,rss=,vsz=,%cpu=,time= 2>/dev/null || true
        fi
    } > "$output"
}

run_case() {
    experiment=$1
    variant=$2
    trial=$3
    case_connections=$4
    case_workers=$5
    case_shards=$6
    case_value_size=$7
    case_read_ratio=$8
    case_durability=$9
    case_order=$((case_order + 1))
    case_seed=$((base_seed + trial - 1))
    name="${experiment}-${variant}-trial-${trial}"
    data_dir="$temporary_root/$name"
    output_prefix="$run_dir/$name"
    resource_file="$output_prefix-server-resource.txt"
    mkdir -p "$data_dir"
    stop_server
    "$build_dir/forgekv-server" --host 127.0.0.1 --port "$port" --data "$data_dir" \
        --workers "$case_workers" --queue-capacity 4096 --max-connections 2048 \
        --index-shards "$case_shards" --durability "$case_durability" \
        >"$output_prefix-server.log" 2>&1 &
    server_pid=$!
    ready=0
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        "$build_dir/forgekv-cli" 127.0.0.1 "$port" GET readiness-probe >/dev/null 2>&1
        status=$?
        if [ "$status" -eq 0 ] || [ "$status" -eq 3 ]; then
            ready=1
            break
        fi
        attempt=$((attempt + 1))
        sleep 0.02
    done
    if [ "$ready" -ne 1 ]; then
        printf '%s\n' "$run_id,$case_order,$experiment,$variant,$trial,$case_seed,invalid,server-not-ready,$name,$name-server-resource.txt" >> "$manifest"
        return
    fi
    "$build_dir/forgekv-bench" network --host 127.0.0.1 --port "$port" \
        --connections "$case_connections" --threads "$case_connections" \
        --requests "$requests" --read-ratio "$case_read_ratio" --key-count 1000 \
        --value-size "$case_value_size" --pipeline-depth 4 --warmup-requests 500 \
        --seed "$case_seed" --server-workers "$case_workers" --server-shards "$case_shards" \
        --durability "$case_durability" --repetition "$trial" \
        --run-id "$run_id" --experiment "$experiment" --variant "$variant" \
        --ram-description "$ram_description" --storage-medium "$storage_medium" \
        --output-prefix "$output_prefix" \
        >"$output_prefix-table.txt" 2>"$output_prefix-error.log"
    status=$?
    capture_resources "$resource_file"
    if [ "$status" -eq 0 ]; then
        printf '%s\n' "$run_id,$case_order,$experiment,$variant,$trial,$case_seed,valid,none,$name,$name-server-resource.txt" >> "$manifest"
    else
        printf '%s\n' "$run_id,$case_order,$experiment,$variant,$trial,$case_seed,invalid,benchmark-exit-$status,$name,$name-server-resource.txt" >> "$manifest"
    fi
}

for trial in $(awk -v n="$trials" 'BEGIN {for (i=1; i<=n; ++i) print i}'); do
    for value in $connections; do
        run_case connections "$value" "$trial" "$value" 4 16 128 0.8 periodic
    done
    for value in $workers; do
        run_case workers "$value" "$trial" 10 "$value" 16 128 0.8 periodic
    done
    for value in $shards; do
        run_case shards "$value" "$trial" 10 4 "$value" 128 0.8 periodic
    done
    for value in $values; do
        run_case value-size "$value" "$trial" 10 4 16 "$value" 0.8 periodic
    done
    for value in $mixes; do
        run_case read-ratio "$value" "$trial" 10 4 16 128 "$value" periodic
    done
    for value in $durability_modes; do
        run_case durability "$value" "$trial" 10 4 16 128 0.8 "$value"
    done
done

python3 "$FORGEKV_BENCH_ROOT/scripts/summarize-benchmark.py" "$run_dir"
echo "benchmark matrix preserved under $run_dir"
