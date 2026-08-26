# Current Limitations

ForgeKV is at Milestone 0. It currently has no key-value operations, persistence, storage format,
recovery, networking, protocol, concurrency, backpressure, TTL expiration, compaction, benchmark
workloads, fuzz targets, or distributed behavior.

The executable targets only report their version and skeleton status. The sole unit test validates
the shared library's version API. Sanitizer options exist, but this does not substitute for the
subsystem-specific test suites planned later.

The repository makes no claims of production readiness, availability, consistency, durability,
fault tolerance, or measured performance.
