# Benchmark Methodology

## Client and outputs

`forgekv-bench network` drives ForgeKV's framed TCP protocol. It supports host, port, connection and
thread counts, request and duration bounds, read ratio, key count, value size, pipeline depth,
warm-up requests, deterministic seed, repetition and server metadata. It preloads the declared key
set before warm-up unless `--skip-preload` is used for a separately prepared profiling run. Each
worker owns its connections; no socket is concurrently used by two threads.

A pipeline is sent as one byte stream and responses are validated in request order. Reported
latency is batch-completion latency assigned to each operation in that pipeline. It must not be
interpreted as independent server service time. Output consists of a terminal table, JSON metadata,
one-row CSV, and an unaggregated latency CSV. Percentiles use nearest-rank over all retained samples.

The build records Git SHA, dirty-worktree state, compiler, configuration, OS/architecture and
hardware thread count. CLI metadata covers run ID, experiment/variant, trial, server
workers/shards/durability, RAM description and storage medium. An unspecified field remains
`unspecified`; the tool never invents hardware facts.

## Matrix runner

`scripts/run-benchmark-matrix.sh quick` runs bounded smoke cases across connections, workers,
shards, value size, workload mix and durability. It requires binaries from a CMake `Release` build,
uses three trials by default, varies the deterministic seed by trial, refuses to overwrite an
existing run ID, and writes artifact-relative manifest paths. `full` uses five trials by default and
the roadmap values:

- connections: 1, 10, 50, 100, 250, 500, 1000;
- workers: 1, 2, 4, 8, 16;
- shards: 1, 4, 16, 64, 256;
- values: 16 B, 128 B, 1 KiB, 16 KiB;
- reads: 100%, 95%, 80%, 50%, 0%;
- durability: always, periodic, none.

Every invocation creates a timestamp/SHA run directory. Its manifest retains case order, trial,
seed, valid cases and invalid cases with a reason. Each trial preserves server logs, stderr, raw
latencies and a resource sidecar. Linux sidecars include `/proc` peak/current virtual and resident
memory, thread count, I/O counters and open-descriptor count; other hosts retain the available `ps`
snapshot. `summary.csv` reports min/median/max throughput, median/max p99 latency, and error totals
across valid trials. Server logs, summaries and raw samples are never overwritten.

The runner defaults to `build-release`; override it with `FORGEKV_BENCH_BUILD_DIR`. A publishable run
must also set `FORGEKV_BENCH_STORAGE_MEDIUM` to a verified description. Trial count, base seed, port
and run ID can be overridden through the documented `FORGEKV_BENCH_*` environment variables in the
script. Example:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
FORGEKV_BENCH_STORAGE_MEDIUM="verified local NVMe model" \
  scripts/run-benchmark-matrix.sh quick
```

## Linux profiling workflow

`scripts/profile-read-heavy.sh` prepares and warms a 100%-GET dataset, then profiles only the
measured workload. It accepts `Release` or `RelWithDebInfo` binaries. On Linux it records `perf`
task-clock call stacks when a usable binary is available, produces a text report, and generates an
SVG flame graph when Brendan Gregg's `stackcollapse-perf.pl` and `flamegraph.pl` are supplied through
`FLAMEGRAPH_DIR`. A separate `strace -f -c` pass attributes system calls. Raw profiler data, workload
JSON/CSV/latencies, environment metadata and resource counts are retained under a unique run ID.

Docker Desktop's LinuxKit kernel does not expose hardware cycle/instruction counters to this run;
the preserved capture therefore uses software task-clock sampling and states that boundary. On a
native Linux host, use the host-matched `perf` package and record the storage device separately.

## Preserved bounded evidence

The current Release smoke matrix under
`bench/raw/matrix-20260905T214247Z-6143f5a796760b818e30f907a3f2dd87373b7d3c/` contains three
trials for each of 14 variants: 42/42 valid trials, zero reported operation/connection errors, raw
latencies, server logs, a manifest, resource sidecars and a distribution summary. It ran from a
dirty macOS development tree with an explicitly unverified APFS storage description, so it proves
the harness contract rather than publishable capacity.

The earlier quick local matrix under `bench/raw/matrix-20260826T235917Z-*` contains 14 valid single-
repetition cases of 2,000 measured operations each, with zero operation and connection errors. It
was an uncommitted development-tree smoke run on the local macOS host. Observed throughput ranged
from roughly 15.2k to 51.8k operations/s across deliberately different cases. Those numbers are not
a capacity result: cases were short, ordered, non-randomized, single-repetition, shared one host,
and included preload/filesystem-cache effects. Its JSON correctly records
`working_tree_dirty: true`. Counterintuitive durability ordering in the earlier development matrix
is retained as evidence of noise, not an optimization claim.

Valid claims are limited to: the benchmark exercised all quick axes, emitted complete raw samples,
and observed no client-reported errors. A publishable performance claim requires the `full` matrix,
multiple randomized repetitions, controlled power/thermal state, explicit RAM/storage metadata,
resource profiles, and a clean committed build.

## Bounded resource profile

A separate five-second periodic-durability run is preserved as
`bench/raw/m10-profile-20260826.{json,csv}` with its unaggregated samples in
`bench/raw/m10-profile-20260826-latency-us.csv`. It completed 263,044 operations at 52,595.6
operations/s with zero operation or connection errors. Observed latency was 682.917 us p50,
1,106.33 us p95, 1,692.17 us p99 and 13,369.3 us maximum.

BSD `time -l` output is preserved in `bench/reports/m10-server-resource-20260826.txt`; it reports a
2,490,368-byte maximum resident set size, 1,982,776-byte peak memory footprint and zero swaps. Its
26.42-second wall time includes benchmark setup and idle time, so it is not a five-second CPU-cost
measurement. A three-second `sample` capture during load is preserved in
`bench/reports/m10-server-sample-20260826.txt`. It observed maintenance threads predominantly
waiting and included periodic-fsync samples, but it is not a complete flame graph or proof of a
specific bottleneck. These are bounded diagnostic observations, not comparative optimization or
capacity claims.

## Optimization discipline

The September 5 Linux-container investigation preserves `before -> hypothesis -> profile evidence
-> change -> after`. `strace` confirmed one `openat`/seek/read/close sequence per GET, but descriptor
caching and removal of the worker handoff did not improve the repeated end-to-end distribution and
were removed. The consistent roughly 40--50 ms batch latency instead identified a small-response
Nagle/delayed-ACK interaction. Five alternating trials against the named baseline commit and the
`TCP_NODELAY` candidate are under
`bench/raw/profile-read-heavy-tcp-nodelay-comparison-20260905T220859Z-6143f5a79676/`.

For that bounded 100%-GET, durability-none, 16-connection, pipeline-depth-four experiment, median
throughput changed from 1,478.52 to 137,599 operations/s (93.07x), and median p99 batch latency from
49,162.4 to 985.708 us (-97.99%), with zero reported errors in either variant. These are comparative
loopback results from a dirty development tree in a Docker Desktop Linux VM, not a production
capacity claim or durable-write result. Future changes must retain the same evidence discipline;
`none` results must never be labeled durable writes.
