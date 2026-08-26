# ForgeKV

ForgeKV is a systems-engineering project in modern C++ that is planned to become a persistent
key-value store with a log-structured storage engine, concurrent request processing, framed TCP
networking, crash recovery, TTL expiration, compaction, and measured performance.

The repository currently contains **Milestone 0 only**: the C++20 build foundation, reusable
library target, executable skeletons, GoogleTest integration, warnings, sanitizer options, and
architecture/roadmap documentation. It does not yet store data or accept network connections.

## Current targets

| Target | Current behavior |
|---|---|
| `forgekv` | Reusable library skeleton and version API |
| `forgekv-server` | Prints its version and implementation status |
| `forgekv-cli` | Prints its version and implementation status |
| `forgekv-bench` | Prints its version and implementation status |
| `forgekv_unit_tests` | Runs the Milestone 0 smoke test |

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

Every box after the executable skeletons is roadmap work, not a current capability. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), [`docs/ROADMAP.md`](docs/ROADMAP.md), and
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md) for precise status and intended sequencing.

## Engineering principles

- Correctness and failure behavior are specified before performance claims.
- No storage engine is hidden beneath ForgeKV.
- Dependencies must have a narrow, documented purpose.
- Benchmarks preserve raw results and environment metadata.
- Distributed work starts only after the single-node engine is tested and measured.

No performance, durability, availability, or production-readiness claims are made at Milestone 0.
