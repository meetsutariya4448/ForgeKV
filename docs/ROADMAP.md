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

## Milestone 4 — Durability and TTL (complete)

Added explicit `always`, `periodic`, and `none` synchronization modes with documented acknowledgement
and loss boundaries. PUTEX/TTL use a sequence-tagged min-heap plus read-time expiration. Version 2
records persist absolute deadlines while mixed v1/v2 replay preserves existing data. Tests cover
stale deadlines, restart, expired replay, periodic/clean-close sync, an always-mode child `_exit`,
and maintenance-thread shutdown. Normal and UBSan verification pass; the documented local ASan/TSan
runtime limitations remain.

## Milestone 5 — Segments and compaction (complete)

Writes rotate at a configurable byte threshold. A background or explicit compactor copies live
records outside the writer critical section, conditionally republishes locations under the segment
publication lock, and retires old segments only after replacement publication. `.old`/`.compact`
startup cleanup rolls an interrupted pre-commit publication back or finishes a visible replacement.
Tests cover rotation/restart, byte reduction, concurrent reads/writes, and both cleanup states.

## Milestone 6 — Performance engineering (complete)

`forgekv-bench network` supports every planned workload bound, persistent connections and client
pipelining. It prints a table and exports JSON, summary CSV, and every raw latency sample. The matrix
runner covers connection, worker, shard, value-size, read/write-mix, and durability axes while
preserving valid and invalid runs. The checked-in quick matrix is explicitly smoke-scale.

## Milestone 7 — Test pyramid completion (complete)

The suite adds deterministic seeded reference models for sequential operations, concurrent
sequence-ordered mutations, and restart-between-batch recovery. Crash, compaction, protocol,
failure, cluster and replication regressions supplement the two libFuzzer targets. Normal and UBSan
pass locally. ASan/TSan build but the documented local macOS runtimes fail before test discovery;
Linux CI is configured to run them, and no unobserved CI pass is claimed.

## Milestone 8 — Distributed sharding (complete library layer)

The cluster library provides a deterministic FNV-derived consistent-hash ring, configurable virtual
nodes, distinct RF placement, membership add/remove, remap fractions, and explicit unreachable-node
failure. Tests verify deterministic placement and the minimal-movement property. Routing alone is
not availability and is not yet connected to the TCP server executable.

## Milestone 9 — Replication (complete protocol/model layer)

A checksummed, bounded replication message format carries primary, per-key-stream sequence,
operation, deadline, key and value. RF=1/2/3 placement supports explicit `primary` and `all`
acknowledgements, duplicate/gap detection, snapshot restart, history recovery, lag metrics, and
slow/unavailable endpoint simulation. This is an in-process transport model—not quorum, consensus,
safe failover, or a deployed multi-process replicated service.

## Milestone 10 — Final benchmark and failure report (complete bounded evidence)

A 14-case quick matrix is preserved with raw samples and zero operation/connection errors. Tests
measure refusal, reset and response-timeout detection plus restart and abrupt-exit recovery. The
report deliberately rejects capacity conclusions from one short local repetition and supplies the
full matrix command for future controlled hosts.

## Milestone 11 — Recruiter-grade finalization (complete)

The README, architecture, format/protocol documents, benchmark/failure reports, demo, interview
guide and limitations distinguish implemented behavior from models and unsupported claims. GitHub
Actions workflows encode normal macOS/Linux, Linux sanitizer and fuzz-smoke builds; workflow success
must be observed remotely before anyone describes CI as passing.
