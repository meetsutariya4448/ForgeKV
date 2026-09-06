# Current Limitations

ForgeKV completes the educational roadmap through Milestone 11, but the implementation has clear
boundaries. It is not production-ready, highly available, consensus-backed, or proven at scale.

## Storage and durability

Only `always` waits for segment and pending directory fsync before acknowledging a mutation.
`periodic` acknowledgements can be lost until a successful sync, and its configured interval is not
a hard loss bound. `none` never fsyncs. There are no transactions, batches, compare-and-swap,
multi-record atomicity, encryption, authentication, or online repair
for a complete bad checksum. The `_exit` test is not a power-cut/controller-cache simulator.

One process owns a database directory at a time through a nonblocking advisory lock on the persistent
`.forgekv.lock` file. A second process fails before recovery or segment mutation, and the kernel
releases ownership when the holder closes or exits. This protects cooperating ForgeKV processes on
local POSIX filesystems; it is not a lease, fencing token, or guarantee for filesystems whose lock
semantics differ. A child created with `fork()` inherits the open lock description until it closes
or executes (the descriptor is close-on-exec), so such children can intentionally or accidentally
extend the ownership lifetime.

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
portable deadline. Established sockets use `TCP_NODELAY`, favoring small request/response latency at
the possible cost of additional packets. CLI arguments are textual even though library and wire
values are binary. The per-call receive timeout lets shutdown be observed but is not a total idle
deadline: an incomplete-frame client can retain its bounded connection slot until it disconnects,
and enough such clients cause new connections to be closed.

STATS is a point-in-time JSON snapshot, not a stable schema or metrics endpoint. Counters reset on
restart and are not persisted.

## Compilers, fuzzers and sanitizers

The local Apple Clang environment is not the sanitizer evidence source. GitHub Actions run
`34016861591` passed 122/122 ASan+UBSan tests, 122/122 TSan tests and both 10,000-run fuzz jobs on
Ubuntu 24.04 for final implementation commit `bdd54e020c1f15559f9102c36c0f1432e16b3b90`.
The Release suite also passed 122/122 tests on Ubuntu 24.04 and macOS 15 in that run. Earlier run
`33884876502` remains historical evidence for the preceding 121-test commit, not the final tree.

## Benchmark evidence

The current quick smoke matrix uses three repeated trials, but it remains short and runs on an
uncommitted local working tree. It validates the harness and raw-output contract, not capacity or
comparative superiority. Pipeline latency is batch completion. The Linux runner captures RSS,
virtual memory, threads, descriptors and process I/O; the profiler adds task-clock samples and
syscall attribution. Thermal state, energy and controlled native storage are still absent, and the
full matrix has not run on a dedicated host.

## Distributed layer

Consistent hashing and replication are reusable in-process library components, not integrated
multi-process service behavior. Membership is supplied as a vector; there is no discovery,
configuration consensus, rebalancing transfer, hinted handoff, read repair, anti-entropy or durable
replica journal. Delay/unavailability are deterministic endpoint simulations, not real WAN tests.

`primary` acknowledgement may leave replicas behind. `all` can report failure after partial
application. There is no quorum read/write mode, idempotency token, fencing, leader election,
automatic failover, split-brain prevention, consensus, linearizability, serializability or causal
consistency claim. Routing deliberately fails when its selected primary is unavailable.
