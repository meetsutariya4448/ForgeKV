# Architecture

## Status legend

- **Implemented:** present and exercised through the current milestone.
- **Planned:** architectural direction that must be validated in its milestone.

## System context

```text
                         implemented TCP
┌──────────────┐      ┌──────────────────┐      ┌─────────────────────┐
│ CLI client   │─────>│ bounded conns.   │─────>│ queue + worker pool │
│ [impl. M2]   │      │ [impl. M3]       │      │ [impl. M3]          │
└──────────────┘      └──────────────────┘      └──────────┬──────────┘
                                                         │
                       ┌─────────────────────────────────┼───────────────┐
                       v                                 v               v
              ┌────────────────┐               ┌────────────────┐ ┌─────────────┐
              │ sharded index  │               │ storage engine │ │ TTL heap    │
              │ [impl. M3]     │<─────────────>│ [impl. M1/M3]  │ │ [planned M4]│
              └────────────────┘               └───────┬────────┘ └─────────────┘
                                                      v
                                              ┌────────────────┐
                                              │ segment files  │
                                              │ recovery       │
                                              │ compaction M5  │
                                              └────────────────┘
```

The shared library, storage engine, sharded index, versioned network codec, incremental parser,
bounded concurrent POSIX server/client, fixed worker pool, CLI, and focused contention benchmark are
implemented. TTL, durability modes, rotation, compaction, and the full workload benchmark remain
planned.

## Single-node boundaries

### Protocol and network

The network layer owns listening and connected sockets through RAII classes. The accept loop creates
at most the configured number of connection `std::jthread`s. Each parses a versioned, length-delimited
binary protocol with bounded key/value/frame sizes and handles partial reads/writes, timeouts,
cancellation polling, and clean shutdown. Frames own decoded bytes; no protocol view outlives its
input buffer. See `PROTOCOL.md` and `CONCURRENCY.md`.

### Request dispatch and backpressure

Validated requests are submitted without blocking to a fixed worker pool through a bounded FIFO
queue. A full queue returns `OVERLOADED`; an excess connection is accepted and immediately closed.
Connection threads wait for one request result at a time, preserving per-connection response order.
The server admits no detached threads or unbounded task accumulation.

### Storage engine

The engine uses a global mutation mutex to serialize the active append stream and sequence. Records
contain type, lengths, sequence, independent header/payload checksums, key, and value. Writes append
and flush before publishing a new sharded-index location. Concurrent reads copy a location and then
read the append-only segment without holding an index lock. Recovery replays complete valid records,
rebuilds the index, truncates a proven incomplete final record, and fails loudly on complete-record
corruption. See `STORAGE_FORMAT.md`.

### Index

The index consists of independently locked shards. Each owns a `std::unordered_map` from an owned key
to a copied record location and protects it with `std::shared_mutex`. GET uses shared access; PUT and
DELETE use exclusive access for one shard. No operation holds two shard locks. Shard count is
configurable; the provisional default is 16 and its evidence boundary is documented in
`CONCURRENCY.md`.

### Expiration

A min-heap is the initial planned expiration structure. Heap entries will carry enough identity
to recognize stale entries after a key update. Reads must also reject expired values, so scheduler
delay cannot make expired data visible.

### Compaction

Compaction will copy only records that remain current, then durably publish replacement segment
metadata before retiring old files. The detailed synchronization and crash protocol are deferred
until segment/recovery invariants exist and can be tested.

## Ownership model

The server process owns one `TcpServer`, which owns the listener, `StorageEngine`, `WorkerPool`,
accepted connection threads, and their descriptors. The pool owns worker threads and queued tasks.
The application owns and joins the server thread used for signal-driven shutdown. Shutdown joins
connections, drains and joins workers, then releases storage. Raw owning pointers and detached
threads remain excluded.

## Planned distributed boundary

Milestones 8 and 9 may add an explicit cluster router, consistent-hash ring, and primary/replica
protocol after single-node correctness and benchmarks. No consensus, leader election,
linearizability, automatic safe failover, or split-brain prevention is implied by that roadmap.
