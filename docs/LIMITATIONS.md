# Current Limitations

ForgeKV is at Milestone 3. It adds a fixed worker pool, bounded queue, configurable sharded index,
bounded concurrent connections, graceful draining, deterministic overload behavior, and a focused
contention benchmark to the persistent engine and framed TCP stack.

It does not yet implement process-level file locking, TTL expiration, segment rotation, compaction,
durability modes, a full end-to-end latency/workload benchmark, or distributed behavior. The current
benchmark isolates index contention and must not be presented as server throughput.

Writes call C++ stream `flush()` but not `fsync`; acknowledged writes may be lost on power or kernel
failure. There are no transactions or multi-record atomicity guarantees. A complete/torn record with
an invalid checksum fails open and currently requires manual/offline intervention. Only the initial
segment ID is opened; extra segment files are not replayed.

GET performs a fresh file open/read and checksum verification rather than caching file descriptors or
values. This favors understandable correctness over performance and has not been benchmarked.

All PUT/DELETE operations still serialize on one append/sequence mutex, even when their keys map to
different shards. Sharding reduces index contention and permits concurrent GET lookup/read work; it
does not parallelize the single active log. Index lookup currently constructs an owned temporary key
because heterogeneous `unordered_map` lookup is not implemented.

The record decoder fuzz target is configured, but the local Apple Clang installation lacks the
libFuzzer runtime; the protocol parser target has the same limitation. UBSan is locally usable; the
previously documented ASan/TSan runtime problems remain.

The server uses one bounded `std::jthread` per active connection rather than an event loop. Default
limits are 128 connections, 256 queued requests, four workers, and 16 shards. Excess connections are
closed without a protocol response; a full request queue returns `OVERLOADED`. Each connection has
only one in-flight worker request, so protocol request IDs do not yet enable parallel pipelining.
Configured bounds prevent unlimited growth but maximum-sized frames across many connections can
still require substantial memory. There is no fairness or latency guarantee under overload.

Shutdown waits for accepted storage work and for socket timeouts to let idle connections observe
cancellation; it does not forcibly interrupt an append. Blocking `connect()` has no strict portable
deadline. The CLI accepts text arguments; arbitrary binary data is supported by the protocol/client
API but not exposed as CLI hex or file-input options yet.

The repository makes no claims of production readiness, availability, strong consistency, stable-
storage durability, fault tolerance, or measured performance.
