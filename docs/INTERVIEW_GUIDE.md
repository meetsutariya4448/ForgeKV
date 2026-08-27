# Systems Interview Guide

## Storage and crash recovery

Be ready to draw the 44-byte v2 record, state both CRC coverage ranges, and explain why only a
provably incomplete tail is truncated while a complete bad checksum fails recovery. The index owns
keys and copied locations; segments own values. A GET copies the location, holds the segment-set read
lock, reads bytes, verifies payload CRC and key identity, then returns an owned value.

Explain the three durability boundaries precisely. `always` fsyncs the record and pending directory
metadata before acknowledgement. `periodic` acknowledges first and may lose an unbounded interval
if synchronization keeps failing. `none` never requests fsync. None is a transaction guarantee.

## Concurrency and compaction

Mutations serialize sequence and append through one mutex; sharded locks protect only index maps.
TTL heap entries carry sequence so an old deadline cannot erase an overwrite. Compaction copies
outside the mutation mutex and republishes with compare-sequence updates. The segment-set shared
lock prevents a GET from combining an old location with deleted old files. Describe `.compact` and
`.old` rename recovery and why the replacement name is the commit point.

## Networking and backpressure

TCP has no message boundaries, so the parser first validates the fixed header and bounded lengths,
then buffers exactly one payload. A poisoned parser closes the connection. Connection count, frame
size, work queue and worker count are bounded. Excess connections close; a full request queue returns
`OVERLOADED`. Pipelined requests preserve per-connection order.

## Benchmark reasoning

Nearest-rank percentiles come from retained raw samples. Pipeline latency is batch completion, not
individual service time. The quick matrix proves the harness works, not that one setting wins. A
credible optimization needs controlled repetitions and before/profile/hypothesis/change/after.

## Distributed reasoning

Consistent hashing limits remapping but provides no replication or availability. RF placement walks
to distinct physical nodes. `primary` acknowledgement can lose acknowledged data if that primary is
lost before replicas catch up; `all` reduces that window but can fail after partial application.
Per-key-stream sequences expose gaps. There is no quorum, consensus, leader election, automatic safe
failover or linearizability, and the current transport is an in-process model.

## Strong resume-safe statement

“Built a C++20 log-structured key-value store with checksummed recovery, configurable fsync,
rotating segments and crash-safe compaction; implemented a bounded framed TCP server, model-based
stress tests, raw-sample benchmarks, consistent-hash placement, and an explicit replication
state-machine model.”
