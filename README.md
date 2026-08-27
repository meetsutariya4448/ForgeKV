# ForgeKV

ForgeKV is a systems-engineering project in modern C++ that is planned to become a persistent
key-value store with a log-structured storage engine, concurrent request processing, framed TCP
networking, crash recovery, TTL expiration, compaction, and measured performance.

The implementation roadmap through **Milestone 11** is present: the bounded single-node server has
versioned persistence, TTL, rotating segments, crash-safe compaction, observability, and a real TCP
benchmark; the reusable cluster library adds deterministic consistent hashing and an explicit
primary/replica protocol model. The cluster layer is not wired into the server process and is not a
consensus or automatic-failover system.

## Current targets

| Target | Current behavior |
|---|---|
| `forgekv` | Storage, compaction, TTL, TCP, concurrency, consistent-hash, and replication library |
| `forgekv-server` | Bounded server for PUT/GET/DELETE/EXISTS/PUTEX/TTL/PING/STATS over TCP |
| `forgekv-cli` | Sends text commands through the binary protocol |
| `forgekv-bench` | Runs pipelined TCP workloads or the focused index-contention experiment |
| `forgekv_unit_tests` | Unit, integration, crash, concurrency, cluster, and model-stress tests |

## Build and test

Prerequisites are a C++20 compiler, CMake 3.24 or newer, Git, and network access during the first
test build. GoogleTest is pinned and fetched into the build tree by default; it is not a runtime
dependency. A compatible installed package may be selected explicitly with
`-DFORGEKV_USE_SYSTEM_GTEST=ON`.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

For an AddressSanitizer plus UndefinedBehaviorSanitizer build:

```sh
cmake -S . -B build-asan -DFORGEKV_ENABLE_ASAN=ON -DFORGEKV_ENABLE_UBSAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

ThreadSanitizer is deliberately configured as a separate build:

```sh
cmake -S . -B build-tsan -DFORGEKV_ENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

## Architecture

```text
clients -> framed TCP server -> bounded dispatcher -> storage engine
                                      |                    |
                                      v                    v
                              sharded index          append-only segments
                                      |                    |
                                      v                    v
                                TTL scheduler       rotation / compaction
```

The single-node path above is implemented. A separate library layer implements a virtual-node hash
ring, RF=1/2/3 placement, replication framing, per-key-stream gap detection, lag/recovery, and
`primary`/`all` acknowledgement modes. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), [`docs/ROADMAP.md`](docs/ROADMAP.md), and
[`docs/STORAGE_FORMAT.md`](docs/STORAGE_FORMAT.md) for precise status and intended sequencing. The
wire contract is in [`docs/PROTOCOL.md`](docs/PROTOCOL.md); current guarantee boundaries are in
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md), and concurrency invariants are in
[`docs/CONCURRENCY.md`](docs/CONCURRENCY.md).

## Run the server and CLI

```sh
./build/forgekv-server --host 127.0.0.1 --port 7391 --data ./forgekv-data \
  --workers 4 --queue-capacity 256 --max-connections 128 --index-shards 16 \
  --durability periodic --sync-interval-ms 1000
./build/forgekv-cli 127.0.0.1 7391 PUT greeting hello
./build/forgekv-cli 127.0.0.1 7391 PUTEX session 5000 token
./build/forgekv-cli 127.0.0.1 7391 GET greeting
./build/forgekv-cli 127.0.0.1 7391 EXISTS greeting
./build/forgekv-cli 127.0.0.1 7391 TTL session
./build/forgekv-cli 127.0.0.1 7391 DELETE greeting
./build/forgekv-cli 127.0.0.1 7391 PING
./build/forgekv-cli 127.0.0.1 7391 STATS
```

Run a bounded TCP benchmark and preserve summary plus raw latency evidence:

```sh
./build/forgekv-bench network --host 127.0.0.1 --port 7391 \
  --connections 10 --threads 10 --requests 100000 --read-ratio 0.8 \
  --key-count 1000 --value-size 128 --pipeline-depth 4 \
  --output-prefix bench/raw/my-run
./scripts/run-benchmark-matrix.sh quick
```

The full required sweep values are available with `./scripts/run-benchmark-matrix.sh full`. Runs are
never overwritten; failures remain in each matrix manifest. Methodology and the bounded local
evidence are in [`docs/BENCHMARKING.md`](docs/BENCHMARKING.md) and
[`docs/FAILURE_REPORT.md`](docs/FAILURE_REPORT.md).

The original isolated shard-contention sweep remains available:

```sh
./scripts/run-m3-contention-benchmark.sh
```

The script writes a uniquely named CSV under `bench/raw/`; it is not end-to-end server throughput.

## Engineering principles

- Correctness and failure behavior are specified before performance claims.
- No storage engine is hidden beneath ForgeKV.
- Dependencies must have a narrow, documented purpose.
- Benchmarks preserve raw results and environment metadata.
- Distributed work starts only after the single-node engine is tested and measured.

Only the documented single-record `always` fsync boundary is called durable. The preserved quick
matrix is smoke-scale evidence, not a capacity claim. No transactional, consensus, automatic
failover, linearizability, arbitrary-filesystem, or production-readiness claims are made.
