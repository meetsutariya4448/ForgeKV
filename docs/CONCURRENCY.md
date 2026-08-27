# Concurrency and Backpressure

This document specifies ForgeKV's Milestone 3 concurrency behavior. It describes implemented
guarantees, not a future asynchronous design.

## Request path and ownership

```text
accept/poll thread
        |
        | accepts at most max_connections
        v
bounded connection jthread (one per active connection)
        |
        | parses one bounded frame and submits without waiting for queue space
        v
bounded FIFO task queue --full--> OVERLOADED response
        |
        v
fixed worker jthreads
        |
        v
thread-safe StorageEngine and sharded index
```

`TcpServer` owns the listener, `StorageEngine`, `WorkerPool`, and every connection `std::jthread`.
The pool owns its worker `std::jthread`s and queue; the queue owns submitted callables. A connection
frame owns its key/value bytes. A submitted task captures an owned frame copy and a shared promise;
there are no detached threads or raw owning pointers.

Defaults are four workers, queue capacity 256, 128 active connections, and 16 index shards. All four
limits are positive and configurable through `ServerConfig` and `forgekv-server` flags.

## Backpressure

- The accept thread checks the active-connection cap. An excess accepted socket is closed
  immediately. It receives no frame because no trustworthy request ID has been parsed.
- Queue submission is nonblocking. A full or closed queue yields status `OVERLOADED` for that valid
  request; the connection remains usable.
- A connection processes its frames sequentially and waits for each worker result, so it has at most
  one submitted request at a time. Responses remain ordered within a connection.
- Memory is bounded by configured connection/queue counts and protocol frame limits, but a high
  connection cap combined with maximum-sized frames can still reserve substantial memory. The
  current limits are guardrails, not a low-memory guarantee.

The queue is FIFO among successfully submitted tasks. ForgeKV does not promise fairness between
connections, admission priority, or a latency bound under saturation.

## Index synchronization

The index deterministically maps a key to one of `N` shards using FNV-1a modulo `N`. Each shard owns
one `std::unordered_map<std::string, RecordLocation>` and one `std::shared_mutex`.

- `find` takes the selected shard's shared lock and returns an owned `RecordLocation` copy.
- `insert_or_assign` and `erase` take only that shard's exclusive lock.
- No operation acquires two shard locks, so the index has no multi-shard lock order.
- The atomic size counter changes while the exclusive shard lock is held.

The map owns key strings. Locations contain segment ID, offsets, lengths, sequence, and payload
checksum; they never borrow request or map storage. GET releases the shard lock before file I/O, so
a slow read does not block index publication, but holds the segment-set shared lock through the read
so compaction cannot retire its copied location's file.

## Storage ordering and linearization points

One `write_mutex` serializes PUT, PUTEX, DELETE, periodic synchronization, and close because the
active segment and global sequence are single append streams. Shards improve lookup concurrency but
do not make log appends parallel.

- PUT prepares owned data, appends the record, advances the sequence, applies any pre-publication
  `always` sync, then publishes the new location under the shard's exclusive lock. Publication is the
  in-process linearization point.
- PUTEX follows PUT ordering, publishes an absolute deadline with the location, then adds a
  sequence-tagged heap entry. A rare post-append publication/scheduling allocation failure makes the
  engine unavailable until replay, preserving the authoritative log sequence.
- DELETE observes existence while holding `write_mutex`, appends its tombstone, advances the
  sequence, applies any `always` sync, then removes the index entry. Removal is the in-process
  linearization point.
- GET copies the current location under the shard's shared lock. That lookup is its linearization
  point; it may return the previous complete value if a concurrent mutation has appended but not yet
  published.

This is per-operation single-process linearization, distinct from stable-storage durability. The
selected fsync mode determines whether synchronization occurs before or after publication; exact
process/power failure guarantees remain those in `STORAGE_FORMAT.md`.

Lock order for mutations is always `write_mutex`, then one shard mutex, then (for PUTEX) the
expiration-heap mutex. The expiration thread releases the heap mutex before acquiring a shard, so it
cannot invert that order. GET never takes `write_mutex`. Engine shutdown is only safe after request
producers and workers are quiesced; the server enforces that ownership order.

Compaction has a separate single-run mutex. Its copy phase holds the segment-set lock shared without
`write_mutex`, so active-segment writes continue. Publication takes `write_mutex`, then the
segment-set lock exclusive, then one shard at a time for conditional sequence replacement. GET
takes the segment-set lock shared before its shard lookup and holds it through file verification.
Compaction never waits for the mutation mutex while holding a shard lock.

## Shutdown

On a normal stop request, the accept loop stops admitting connections, requests cancellation on all
connection threads, and joins them. Socket timeouts let idle receive loops observe cancellation.
Connections already waiting for a worker result are allowed to finish. The worker queue is then
closed, queued tasks drain, and workers join. Storage atomically closes admission, stops and joins its
periodic-sync, expiration, and compaction threads, performs the durability mode's final sync policy,
and closes the descriptor.

Shutdown is graceful, not instantaneous. Its upper bound includes the socket timeout and the duration
of accepted storage operations. There is no forced cancellation in the middle of an append.

## Contention evidence

`forgekv-bench contention` executes a 90%-find/10%-replace workload for both one hot key and 4,096
distributed keys. The runner warms each case, sweeps 1/4/16/64/256 shards, records three repetitions,
and preserves complete metadata and raw rows.

The first Milestone 3 run is in `bench/raw/`. Its debug-build medians show distributed-key scaling
from roughly 646 thousand operations/second at one shard to 3.82 million at 64 and 4.11 million at
256. Same-key results remain around 0.82–0.89 million operations/second across the sweep because all
accesses contend on one lock. The 256-shard distributed case also had a low outlier near 2.04 million,
so these figures are evidence about contention shape, not stable performance claims.

The default remains 16: it materially reduced distributed-key contention in this small run without
assuming the memory/initialization cost of much larger shard counts. Milestone 6 must repeat shard,
thread, and workload sweeps in optimized builds before changing or promoting that default.
