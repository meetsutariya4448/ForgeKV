# ForgeKV

ForgeKV is a systems-engineering project in modern C++ that is planned to become a persistent
key-value store with a log-structured storage engine, concurrent request processing, framed TCP
networking, crash recovery, TTL expiration, compaction, and measured performance.

The repository currently contains **Milestone 3**: the persistent storage engine and framed TCP
stack now run through a fixed worker pool and bounded queue, with bounded concurrent connections,
deterministic overload behavior, and a configurable sharded index.

## Current targets

| Target | Current behavior |
|---|---|
| `forgekv` | Reusable storage, protocol, index, queue, worker-pool, and network library |
| `forgekv-server` | Concurrent bounded server for PUT/GET/DELETE/EXISTS over TCP |
| `forgekv-cli` | Sends text commands through the binary protocol |
| `forgekv-bench` | Runs the focused Milestone 3 index-contention sweep |
| `forgekv_unit_tests` | Runs concurrency, network, protocol, storage/recovery, and corruption tests |

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

## Planned architecture

```text
clients -> framed TCP server -> bounded dispatcher -> storage engine
                                      |                    |
                                      v                    v
                              sharded index          append-only segments
                                      |                    |
                                      v                    v
                                TTL scheduler          recovery/compaction
```

The bounded dispatcher, sharded index, protocol, TCP server, storage engine, and CLI are implemented;
TTL, durability modes, rotation, and compaction remain roadmap work. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), [`docs/ROADMAP.md`](docs/ROADMAP.md), and
[`docs/STORAGE_FORMAT.md`](docs/STORAGE_FORMAT.md) for precise status and intended sequencing. The
wire contract is in [`docs/PROTOCOL.md`](docs/PROTOCOL.md); current guarantee boundaries are in
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md), and concurrency invariants are in
[`docs/CONCURRENCY.md`](docs/CONCURRENCY.md).

## Run the server and CLI

```sh
./build/forgekv-server --host 127.0.0.1 --port 7391 --data ./forgekv-data \
  --workers 4 --queue-capacity 256 --max-connections 128 --index-shards 16
./build/forgekv-cli 127.0.0.1 7391 PUT greeting hello
./build/forgekv-cli 127.0.0.1 7391 GET greeting
./build/forgekv-cli 127.0.0.1 7391 EXISTS greeting
./build/forgekv-cli 127.0.0.1 7391 DELETE greeting
```

Run and preserve the Milestone 3 shard-contention sweep after building:

```sh
./scripts/run-m3-contention-benchmark.sh
```

The script writes a uniquely named CSV with Git, compiler, build-command, OS, and hardware metadata
under `bench/raw/`. This is a narrow index experiment, not the full Milestone 6 server benchmark.

## Engineering principles

- Correctness and failure behavior are specified before performance claims.
- No storage engine is hidden beneath ForgeKV.
- Dependencies must have a narrow, documented purpose.
- Benchmarks preserve raw results and environment metadata.
- Distributed work starts only after the single-node engine is tested and measured.

No end-to-end performance, stable-storage durability, availability, or production-readiness claims
are made at Milestone 3.
