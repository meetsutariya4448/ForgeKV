# Current Limitations

ForgeKV is at Milestone 2. It adds a versioned framed TCP protocol, bounded incremental parser,
single-threaded POSIX server, reusable client, text CLI, and binary loopback integration coverage to
the Milestone 1 storage engine.

It does not yet implement concurrent connections, worker queues, connection limits, backpressure,
thread-safe storage, process-level file locking, TTL expiration, segment rotation, compaction,
durability modes, benchmark workloads, or distributed behavior. The benchmark executable remains a
skeleton.

Writes call C++ stream `flush()` but not `fsync`; acknowledged writes may be lost on power or kernel
failure. There are no transactions or multi-record atomicity guarantees. A complete/torn record with
an invalid checksum fails open and currently requires manual/offline intervention. Only the initial
segment ID is opened; extra segment files are not replayed.

GET performs a fresh file open/read and checksum verification rather than caching file descriptors or
values. This favors understandable correctness over performance and has not been benchmarked.

The record decoder fuzz target is configured, but the local Apple Clang installation lacks the
libFuzzer runtime; the protocol parser target has the same limitation. UBSan is locally usable; the
previously documented ASan/TSan runtime problems remain.

Only one client connection is active at a time, so an idle/slow client delays all others. Socket I/O
timeouts are bounded, but blocking `connect()` has no strict portable deadline. The CLI accepts text
arguments; arbitrary binary data is supported by the protocol/client API but not exposed as CLI hex
or file-input options yet.

The repository makes no claims of production readiness, availability, strong consistency, stable-
storage durability, fault tolerance, or measured performance.
