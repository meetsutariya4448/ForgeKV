# Benchmark Methodology

## Client and outputs

`forgekv-bench network` drives ForgeKV's framed TCP protocol. It supports host, port, connection and
thread counts, request and duration bounds, read ratio, key count, value size, pipeline depth,
warm-up requests, deterministic seed, repetition and server metadata. It preloads the declared key
set before warm-up. Each worker owns its connections; no socket is concurrently used by two threads.

A pipeline is sent as one byte stream and responses are validated in request order. Reported
latency is batch-completion latency assigned to each operation in that pipeline. It must not be
interpreted as independent server service time. Output consists of a terminal table, JSON metadata,
one-row CSV, and an unaggregated latency CSV. Percentiles use nearest-rank over all retained samples.

The build records Git SHA, dirty-worktree state, compiler, configuration, OS/architecture and
hardware thread count. CLI metadata covers server workers/shards/durability, RAM description and
storage medium. An unspecified field remains `unspecified`; the tool never invents hardware facts.

## Matrix runner

`scripts/run-benchmark-matrix.sh quick` runs bounded smoke cases across connections, workers,
shards, value size, workload mix and durability. `full` uses the roadmap values:

- connections: 1, 10, 50, 100, 250, 500, 1000;
- workers: 1, 2, 4, 8, 16;
- shards: 1, 4, 16, 64, 256;
- values: 16 B, 128 B, 1 KiB, 16 KiB;
- reads: 100%, 95%, 80%, 50%, 0%;
- durability: always, periodic, none.

Every invocation creates a timestamp/SHA directory. Its manifest retains valid cases and invalid
cases with a reason; server logs, stderr, summaries and raw samples are never overwritten.

## Preserved bounded evidence

The final quick local matrix under `bench/raw/matrix-20260826T235917Z-*` contains 14 valid single-
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

No throughput optimization is claimed from the quick matrix. Future changes must preserve a note
with `before -> hypothesis -> profile evidence -> change -> after`, retain regressions, and compare
identical workload and durability settings. `none` results must never be labeled durable writes.
