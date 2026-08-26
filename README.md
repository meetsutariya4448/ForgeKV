# ForgeKV

ForgeKV is a systems-engineering project in modern C++ that is planned to become a persistent
key-value store with a log-structured storage engine, concurrent request processing, framed TCP
networking, crash recovery, TTL expiration, compaction, and measured performance.

The repository currently contains **Milestone 2**: the persistent storage engine plus a versioned,
checksummed binary TCP protocol, bounded incremental parser, single-threaded server, reusable client,
and functional text CLI. Concurrency and backpressure are not implemented yet.

## Current targets

| Target | Current behavior |
|---|---|
| `forgekv` | Reusable library with record codec, CRC32C, and storage engine API |
| `forgekv-server` | Serves PUT/GET/DELETE/EXISTS over TCP |
| `forgekv-cli` | Sends text commands through the binary protocol |
| `forgekv-bench` | Prints its version and implementation status |
| `forgekv_unit_tests` | Runs protocol, loopback network, storage/recovery, corruption, and smoke tests |

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

The storage engine, unsharded index, protocol, TCP server, and CLI are implemented; bounded dispatch,
concurrency, TTL, rotation, and compaction remain roadmap work. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), [`docs/ROADMAP.md`](docs/ROADMAP.md), and
[`docs/STORAGE_FORMAT.md`](docs/STORAGE_FORMAT.md) for precise status and intended sequencing. The
wire contract is in [`docs/PROTOCOL.md`](docs/PROTOCOL.md); current guarantee boundaries are in
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md).

## Run the server and CLI

```sh
./build/forgekv-server --host 127.0.0.1 --port 7391 --data ./forgekv-data
./build/forgekv-cli 127.0.0.1 7391 PUT greeting hello
./build/forgekv-cli 127.0.0.1 7391 GET greeting
./build/forgekv-cli 127.0.0.1 7391 EXISTS greeting
./build/forgekv-cli 127.0.0.1 7391 DELETE greeting
```

## Engineering principles

- Correctness and failure behavior are specified before performance claims.
- No storage engine is hidden beneath ForgeKV.
- Dependencies must have a narrow, documented purpose.
- Benchmarks preserve raw results and environment metadata.
- Distributed work starts only after the single-node engine is tested and measured.

No performance, stable-storage durability, availability, or production-readiness claims are made at
Milestone 2.
