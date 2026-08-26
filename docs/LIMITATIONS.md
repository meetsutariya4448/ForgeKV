# Current Limitations

ForgeKV is at Milestone 1. It implements a single-process, single-threaded append-only storage engine
with binary PUT/GET/DELETE, checksummed records, one segment, in-memory indexing, restart replay, and
conservative truncated-tail recovery.

It does not yet implement networking, a protocol, concurrent access, process-level file locking,
backpressure, TTL expiration, segment rotation, compaction, durability modes, benchmark workloads,
or distributed behavior. The server/CLI/benchmark executables remain skeletons.

Writes call C++ stream `flush()` but not `fsync`; acknowledged writes may be lost on power or kernel
failure. There are no transactions or multi-record atomicity guarantees. A complete/torn record with
an invalid checksum fails open and currently requires manual/offline intervention. Only the initial
segment ID is opened; extra segment files are not replayed.

GET performs a fresh file open/read and checksum verification rather than caching file descriptors or
values. This favors understandable correctness over performance and has not been benchmarked.

The record decoder fuzz target is configured, but the local Apple Clang installation lacks the
libFuzzer runtime. UBSan is locally usable; the previously documented ASan/TSan runtime problems
remain.

The repository makes no claims of production readiness, availability, strong consistency, stable-
storage durability, fault tolerance, or measured performance.
