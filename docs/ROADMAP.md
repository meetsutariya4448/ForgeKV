# Implementation Roadmap

Milestones are sequential gates. Later work begins only after the preceding milestone has explicit
tests, documentation, and review. Each subsystem must document its design, invariants, failure
behavior, complexity, evidence, and largest known limitation.

## Milestone 0 — Foundation (complete)

Establish C++20/CMake targets, warning and sanitizer configuration, GoogleTest, formatting policy,
repository hygiene, executable skeletons, architecture documentation, and decision/engineering
logs. Exit when normal and supported sanitizer builds pass and the repository has focused commits.

## Milestone 1 — Storage format and recovery (complete)

Specify `docs/STORAGE_FORMAT.md` before implementation. Add bounded binary record encode/decode,
checksums, tombstones, monotonic sequences, a segment writer, an in-memory index, PUT/GET/DELETE,
startup replay, and defined partial-tail/corruption behavior. Unit tests cover every field boundary;
integration tests cover restart, deletion, truncation, and corruption. Exit only when storage and
recovery parsers pass sanitizers and their invariants are interview-defensible.

## Milestone 2 — TCP protocol and CLI (complete)

Specify byte order, widths, versioning, maximums, checksums, response errors, and malformed-frame
behavior in `docs/PROTOCOL.md`. Implement incremental parsing, socket RAII, partial I/O loops,
timeouts/cancellation, server request handling, and a functional CLI. Add loopback integration
tests and a libFuzzer parser target. Exit when binary values and fragmented frames work and invalid
lengths cannot cause unbounded allocation.

## Milestone 3 — Concurrency and backpressure (complete)

Implemented a bounded blocking queue, fixed `std::jthread` worker pool, graceful draining, sharded
`std::unordered_map` index, connection cap, and deterministic overload behavior. Tests cover
same-key/different-key storage and index access, queue saturation, draining shutdown, concurrent TCP
clients, connection rejection, and idle-connection shutdown. A reproducible 1/4/16/64/256 shard
contention sweep is preserved under `bench/raw/`. UBSan passes; TSan instrumentation builds, but the
documented local macOS runtime crashes before GoogleTest discovery, so no TSan pass is claimed.

## Milestone 4 — Durability and TTL

Add explicit `always`, `periodic`, and `none` synchronization modes and document their true loss
windows. Implement PUTEX/TTL using a min-heap with stale-entry detection and read-time expiration.
Persist absolute expiration metadata and define clock/restart semantics. Add abrupt-kill/restart and
TTL recovery tests.

## Milestone 5 — Segments and compaction

Add segment rotation and background compaction that preserves concurrent safety. Specify the
replacement publication protocol, directory sync requirements, and startup cleanup of interrupted
compactions. Test writes/reads during compaction under TSan and crash at publication boundaries.
Record bytes before/after, write amplification, and duration.

## Milestone 6 — Performance engineering

Implement `forgekv-bench` with host, port, connections, threads, request/duration bounds, read
ratio, key count, value size, and pipeline depth. Export tables, JSON, CSV, and latency p50/p95/p99/
max without discarding raw samples. Capture environment and Git metadata. Run concurrency, worker,
shard, value-size, workload-mix, and durability sweeps; preserve all valid and invalidated runs with
reasons. Profile first, then record before/hypothesis/change/after for optimizations.

## Milestone 7 — Test pyramid completion

Expand unit, integration, concurrency, crash, fuzz, and sanitizer coverage. Add a deterministic
seeded reference-model stress harness for single-thread, multi-thread, and restart-between-batch
runs. Exit when supported ASan/UBSan/TSan suites pass and every discovered defect has a regression
test and engineering-log entry.

## Milestone 8 — Distributed sharding

Only after single-node evidence, implement a deterministic consistent-hash ring with virtual nodes,
membership changes, routing, and remapping measurements. Test add/remove placement and failure to
reach a selected node. Document that routing alone provides neither replication nor availability.

## Milestone 9 — Replication

Add configured RF=1/2/3 primary/replica placement, an explicit replication protocol, sequence-gap
detection, replica recovery, lag metrics, and `primary` versus `all` acknowledgement modes. Test
timeouts, ordering gaps, restarts, slow/unavailable replicas, and document data-loss/consistency
windows. Do not claim quorum, consensus, safe failover, or linearizability.

## Milestone 10 — Final benchmark and failure report

Run reproducible single- and multi-node matrices, resource-use profiling, network failure scenarios,
and recovery measurements. Preserve raw results and publish methodology, hardware/software context,
limitations, and only claims directly supported by the data.

## Milestone 11 — Recruiter-grade finalization

Audit every README and resume-safe statement against implementation, tests, and measurements. Add
clear diagrams, a bounded demo, benchmark summaries linked to raw evidence, an interview guide, and
an honest limitations section. Ensure builds and documented workflows are reproducible through CI
before describing CI as passing.
