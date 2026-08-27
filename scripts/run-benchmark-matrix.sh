#!/bin/sh
set -u

mode=${1:-quick}
port=${FORGEKV_BENCH_PORT:-17411}
case "$mode" in
    quick)
        connections="1 10"
        workers="1 4"
        shards="1 16"
        values="16 1024"
        mixes="1.0 0.8 0.0"
        durability_modes="always periodic none"
        requests=2000
        ;;
    full)
        connections="1 10 50 100 250 500 1000"
        workers="1 2 4 8 16"
        shards="1 4 16 64 256"
        values="16 128 1024 16384"
        mixes="1.0 0.95 0.8 0.5 0.0"
        durability_modes="always periodic none"
        requests=100000
        ;;
    *)
        echo "usage: $0 [quick|full]" >&2
        exit 2
        ;;
esac

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
git_sha=$(git rev-parse HEAD 2>/dev/null || echo unknown)
run_dir="bench/raw/matrix-${timestamp}-${git_sha}"
mkdir -p "$run_dir"
manifest="$run_dir/manifest.csv"
printf '%s\n' "experiment,value,status,reason,output_prefix" > "$manifest"
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/forgekv-matrix.XXXXXX")
server_pid=""

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

run_case() {
    experiment=$1
    value=$2
    case_connections=$3
    case_workers=$4
    case_shards=$5
    case_value_size=$6
    case_read_ratio=$7
    case_durability=$8
    name="${experiment}-${value}"
    data_dir="$temporary_root/$name"
    output_prefix="$run_dir/$name"
    mkdir -p "$data_dir"
    stop_server
    ./build/forgekv-server --host 127.0.0.1 --port "$port" --data "$data_dir" \
        --workers "$case_workers" --queue-capacity 4096 --max-connections 2048 \
        --index-shards "$case_shards" --durability "$case_durability" \
        >"$output_prefix-server.log" 2>&1 &
    server_pid=$!
    ready=0
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        ./build/forgekv-cli 127.0.0.1 "$port" GET readiness-probe >/dev/null 2>&1
        status=$?
        if [ "$status" -eq 0 ] || [ "$status" -eq 3 ]; then
            ready=1
            break
        fi
        attempt=$((attempt + 1))
        sleep 0.02
    done
    if [ "$ready" -ne 1 ]; then
        printf '%s\n' "$experiment,$value,invalid,server-not-ready,$output_prefix" >> "$manifest"
        return
    fi
    ./build/forgekv-bench network --host 127.0.0.1 --port "$port" \
        --connections "$case_connections" --threads "$case_connections" \
        --requests "$requests" --read-ratio "$case_read_ratio" --key-count 1000 \
        --value-size "$case_value_size" --pipeline-depth 4 --warmup-requests 500 \
        --server-workers "$case_workers" --server-shards "$case_shards" \
        --durability "$case_durability" --repetition 1 --output-prefix "$output_prefix" \
        >"$output_prefix-table.txt" 2>"$output_prefix-error.log"
    status=$?
    if [ "$status" -eq 0 ]; then
        printf '%s\n' "$experiment,$value,valid,none,$output_prefix" >> "$manifest"
    else
        printf '%s\n' "$experiment,$value,invalid,benchmark-exit-$status,$output_prefix" >> "$manifest"
    fi
}

for value in $connections; do
    run_case connections "$value" "$value" 4 16 128 0.8 periodic
done
for value in $workers; do
    run_case workers "$value" 10 "$value" 16 128 0.8 periodic
done
for value in $shards; do
    run_case shards "$value" 10 4 "$value" 128 0.8 periodic
done
for value in $values; do
    run_case value-size "$value" 10 4 16 "$value" 0.8 periodic
done
for value in $mixes; do
    run_case read-ratio "$value" 10 4 16 128 "$value" periodic
done
for value in $durability_modes; do
    run_case durability "$value" 10 4 16 128 0.8 "$value"
done

echo "benchmark matrix preserved under $run_dir"
