# Current Limitations

ForgeKV completes the educational roadmap through Milestone 11, but the implementation has clear
boundaries. It is not production-ready, highly available, consensus-backed, or proven at scale.

## Storage and durability

Only `always` waits for segment and pending directory fsync before acknowledging a mutation.
`periodic` acknowledgements can be lost until a successful sync, and its configured interval is not
a hard loss bound. `none` never fsyncs. There are no transactions, batches, compare-and-swap,
multi-record atomicity, process-level database locks, encryption, authentication, or online repair
for a complete bad checksum. The `_exit` test is not a power-cut/controller-cache simulator.

Segment rotation has one active writer. All PUT/PUTEX/DELETE, rotation, fsync and compaction
publication still serialize on the mutation mutex. Compaction copies outside that mutex but briefly
blocks segment readers and writers for rename/publication. It compacts all inactive segments rather
than selecting by garbage ratio, has no rate limiting, and can temporarily need input plus output
disk space. Publication assumes same-filesystem atomic rename and ordinary POSIX directory fsync.
`none` mode does not make compaction namespace changes crash-durable.

GET opens a segment file for each request and verifies the whole key+value payload CRC. There is no
descriptor cache, block cache, bloom filter, compression, direct I/O, async I/O, or zero-copy return.
The index stores owned keys and constructs temporary lookup strings. These choices favor explicit
ownership and corruption checks over peak throughput.

## Time and expiration

TTL uses the system wall clock at millisecond precision. Backward adjustment extends apparent TTL;
a forward jump makes GET reject immediately but heap cleanup may wait for its previous wake delay.
Expiration does not append tombstones. Repeated overwrites leave stale heap entries until their old
deadlines, so heap memory can exceed live expiring keys. PUTEX's eight-byte wire prefix reduces its
maximum user value by eight bytes.

## Network and overload

The server uses one `std::jthread` per accepted connection rather than an event loop. Connections,
queue entries, workers and frames are bounded, but maximum-sized frames across the configured
connection limit can still consume substantial memory. Excess connections close without a protocol
response; queue saturation returns `OVERLOADED`. There is no admission fairness, TLS, authentication,
authorization, rate limiting, tenant isolation, or latency SLA. Pipelined requests execute in order
on one connection; they are not parallel within that connection. Blocking `connect()` has no strict
portable deadline. CLI arguments are textual even though library and wire values are binary.

STATS is a point-in-time JSON snapshot, not a stable schema or metrics endpoint. Counters reset on
restart and are not persisted.

## Compilers, fuzzers and sanitizers

Normal and UBSan suites run locally. ASan and TSan builds compile/link, but the local macOS sanitizer
runtimes fail before GoogleTest discovery; no local pass is claimed. The local Apple Clang lacks the
libFuzzer runtime. Linux CI jobs are configured for ASan+UBSan, TSan and fuzz smoke tests, but until a
commit is pushed and observed, CI must be described as configured—not passing.

## Benchmark evidence

The preserved quick matrix is one short, ordered repetition per case on an uncommitted local working
tree. It validates the harness and raw-output contract, not capacity or comparative superiority.
Pipeline latency is batch completion. CPU, RSS, disk bandwidth, syscalls, thermal state and energy
were not captured. The full matrix script exists but has not been run on a dedicated controlled host.

## Distributed layer

Consistent hashing and replication are reusable in-process library components, not integrated
multi-process service behavior. Membership is supplied as a vector; there is no discovery,
configuration consensus, rebalancing transfer, hinted handoff, read repair, anti-entropy or durable
replica journal. Delay/unavailability are deterministic endpoint simulations, not real WAN tests.

`primary` acknowledgement may leave replicas behind. `all` can report failure after partial
application. There is no quorum read/write mode, idempotency token, fencing, leader election,
automatic failover, split-brain prevention, consensus, linearizability, serializability or causal
consistency claim. Routing deliberately fails when its selected primary is unavailable.
