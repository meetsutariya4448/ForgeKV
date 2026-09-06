#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$(uname -s)" != "Linux" ]; then
    echo "read-heavy profiling requires Linux" >&2
    exit 1
fi
build_dir=${FORGEKV_PROFILE_BUILD_DIR:-$root/build-release}
port=${FORGEKV_PROFILE_PORT:-17431}
duration=${FORGEKV_PROFILE_DURATION:-15}
connections=${FORGEKV_PROFILE_CONNECTIONS:-16}
workers=${FORGEKV_PROFILE_WORKERS:-4}
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
git_sha=$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)
run_id=${FORGEKV_PROFILE_RUN_ID:-profile-read-heavy-${timestamp}-${git_sha}}
output_dir=${FORGEKV_PROFILE_OUTPUT_DIR:-$root/bench/raw/$run_id}

case "$duration:$connections:$workers" in
    *[!0-9:]*|0:*|*:0:*|*:0) echo "duration, connections, and workers must be positive integers" >&2; exit 2 ;;
esac
if [ -e "$output_dir" ]; then
    echo "refusing to overwrite profiling output: $output_dir" >&2
    exit 1
fi
if [ ! -x "$build_dir/forgekv-server" ] || [ ! -x "$build_dir/forgekv-bench" ] ||
   [ ! -x "$build_dir/forgekv-cli" ]; then
    echo "profiling binaries are missing under $build_dir" >&2
    exit 1
fi
build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$build_dir/CMakeCache.txt")
case "$build_type" in
    Release|RelWithDebInfo) ;;
    *) echo "profiling requires Release or RelWithDebInfo; found ${build_type:-unknown}" >&2; exit 1 ;;
esac

mkdir -p "$output_dir"
data_dir=$(mktemp -d "${TMPDIR:-/tmp}/forgekv-profile.XXXXXX")
server_pid=""
stop_server() {
    if [ -n "$server_pid" ]; then
        kill -INT "$server_pid" 2>/dev/null || true
        stop_attempt=0
        while kill -0 "$server_pid" 2>/dev/null && [ "$stop_attempt" -lt 100 ]; do
            stop_attempt=$((stop_attempt + 1))
            sleep 0.05
        done
        if kill -0 "$server_pid" 2>/dev/null; then
            kill -KILL "$server_pid" 2>/dev/null || true
        fi
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
    fi
}
cleanup() {
    stop_server
    rm -rf "$data_dir"
}
trap cleanup EXIT INT TERM

"$build_dir/forgekv-server" --host 127.0.0.1 --port "$port" --data "$data_dir" \
    --workers "$workers" --queue-capacity 4096 --max-connections 2048 \
    --index-shards 16 --durability none --no-background-compaction \
    >"$output_dir/server.log" 2>&1 &
server_pid=$!
ready=0
attempt=0
while [ "$attempt" -lt 200 ]; do
    if "$build_dir/forgekv-cli" 127.0.0.1 "$port" GET readiness-probe >/dev/null 2>&1; then
        status=0
    else
        status=$?
    fi
    if [ "$status" -eq 0 ] || [ "$status" -eq 3 ]; then ready=1; break; fi
    attempt=$((attempt + 1))
    sleep 0.02
done
if [ "$ready" -ne 1 ]; then
    echo "server did not become ready" >&2
    exit 1
fi

# Populate and warm the dataset before attaching a profiler.
"$build_dir/forgekv-bench" network --host 127.0.0.1 --port "$port" \
    --connections "$connections" --threads "$connections" --requests 1 \
    --read-ratio 1.0 --key-count 1000 --value-size 128 --pipeline-depth 4 \
    --warmup-requests 5000 --seed 1 >/dev/null

ram_description=$(awk '/MemTotal/ {print $2 " kB"; exit}' /proc/meminfo 2>/dev/null || echo unspecified)
storage_medium=${FORGEKV_PROFILE_STORAGE_MEDIUM:-unspecified}
run_workload() {
    variant=$1
    prefix=$2
    "$build_dir/forgekv-bench" network --host 127.0.0.1 --port "$port" \
        --connections "$connections" --threads "$connections" --requests 0 \
        --duration "$duration" --read-ratio 1.0 --key-count 1000 --value-size 128 \
        --pipeline-depth 4 --warmup-requests 0 --skip-preload --seed 2 \
        --server-workers "$workers" --server-shards 16 --durability none \
        --repetition 1 --run-id "$run_id" --experiment read-heavy-profile \
        --variant "$variant" --ram-description "$ram_description" \
        --storage-medium "$storage_medium" --output-prefix "$prefix"
}

find_perf() {
    if [ -n "${FORGEKV_PERF:-}" ] && [ -x "$FORGEKV_PERF" ]; then
        echo "$FORGEKV_PERF"
        return
    fi
    if command -v perf >/dev/null 2>&1 && perf version >/dev/null 2>&1; then
        command -v perf
        return
    fi
    for candidate in /usr/lib/linux-tools-*/perf /usr/lib/linux-tools/*/perf; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return
        fi
    done
}

perf_binary=$(find_perf)
perf_status=unavailable
if [ -n "$perf_binary" ]; then
    "$perf_binary" record -q -e task-clock -F 99 -g -p "$server_pid" \
        -o "$output_dir/perf.data" -- sleep "$duration" &
    profiler_pid=$!
    run_workload perf "$output_dir/perf-workload" >"$output_dir/perf-workload-table.txt"
    if wait "$profiler_pid"; then
        perf_status=recorded
        "$perf_binary" report -i "$output_dir/perf.data" --stdio --no-children \
            --sort comm,dso,symbol >"$output_dir/perf-report.txt"
        collapse=${FORGEKV_STACKCOLLAPSE_PERF:-}
        flamegraph=${FORGEKV_FLAMEGRAPH:-}
        if [ -z "$collapse" ] && [ -n "${FLAMEGRAPH_DIR:-}" ]; then
            collapse=$FLAMEGRAPH_DIR/stackcollapse-perf.pl
        fi
        if [ -z "$flamegraph" ] && [ -n "${FLAMEGRAPH_DIR:-}" ]; then
            flamegraph=$FLAMEGRAPH_DIR/flamegraph.pl
        fi
        if [ -x "$collapse" ] && [ -x "$flamegraph" ]; then
            "$perf_binary" script -i "$output_dir/perf.data" | "$collapse" \
                >"$output_dir/perf-folded.txt"
            "$flamegraph" "$output_dir/perf-folded.txt" >"$output_dir/flamegraph.svg"
        fi
    else
        perf_status=failed
    fi
else
    run_workload unprofiled "$output_dir/workload" >"$output_dir/workload-table.txt"
fi

strace_status=unavailable
if command -v strace >/dev/null 2>&1 && command -v timeout >/dev/null 2>&1; then
    timeout $((duration + 2)) strace -f -c -o "$output_dir/strace-summary.txt" \
        -p "$server_pid" &
    tracer_pid=$!
    run_workload strace "$output_dir/strace-workload" >"$output_dir/strace-workload-table.txt"
    wait "$tracer_pid" 2>/dev/null || true
    if [ -s "$output_dir/strace-summary.txt" ]; then strace_status=recorded; else strace_status=failed; fi
fi

{
    echo "run_id=$run_id"
    echo "git_sha=$git_sha"
    echo "working_tree_dirty=$(test -n "$(git -C "$root" status --porcelain)" && echo true || echo false)"
    echo "captured_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "system=$(uname -srvmo)"
    echo "build=$build_type"
    echo "compiler=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -n 1)"
    echo "workload=100-percent-GET,1000 keys,128-byte values,$connections connections,$workers workers,pipeline 4,$duration seconds"
    echo "storage_medium=$storage_medium"
    echo "perf=$perf_status"
    echo "perf_binary=${perf_binary:-none}"
    echo "strace=$strace_status"
    if [ -r "/proc/$server_pid/status" ]; then
        sed -n '/^VmPeak:/p;/^VmHWM:/p;/^VmRSS:/p;/^Threads:/p' "/proc/$server_pid/status"
        echo "open_fds=$(find "/proc/$server_pid/fd" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l | tr -d ' ')"
    fi
} >"$output_dir/metadata.txt"

echo "read-heavy profile preserved under $output_dir"
