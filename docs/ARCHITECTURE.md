# Architecture

## Status legend

- **Implemented:** present and exercised through the current milestone.
- **Planned:** architectural direction that must be validated in its milestone.

## System context

```text
                        implemented TCP
┌──────────────┐      ┌──────────────────┐      ┌─────────────────────┐
│ CLI / bench  │─────>│ network server   │─────>│ bounded dispatcher  │
│ CLI impl. M2 │      │ [implemented M2] │      │ [planned M3]        │
└──────────────┘      └──────────────────┘      └──────────┬──────────┘
                                                         │
                       ┌─────────────────────────────────┼───────────────┐
                       v                                 v               v
              ┌────────────────┐               ┌────────────────┐ ┌─────────────┐
              │ sharded index  │               │ storage engine │ │ TTL heap    │
              │ [planned M3]   │<─────────────>│ [implemented M1]│ │ [planned M4]│
              └────────────────┘               └───────┬────────┘ └─────────────┘
                                                      v
                                              ┌────────────────┐
                                              │ segment files  │
                                              │ recovery       │
                                              │ compaction M5  │
                                              └────────────────┘
```

The shared library, storage engine, versioned network codec, incremental parser, POSIX server/client,
CLI, and synchronous request dispatch are implemented. The dispatcher currently runs inline on one
connection; worker queues, sharding, TTL, rotation, and compaction remain planned.

## Single-node boundaries

### Protocol and network

The network layer owns listening and connected sockets through RAII classes. It parses a versioned,
length-delimited binary protocol with bounded key/value/frame sizes. Connection code handles partial
reads, partial writes, timeouts, cancellation polling, and clean shutdown. Frames own decoded bytes;
no protocol view outlives its input buffer. See `PROTOCOL.md`.

### Request dispatch and backpressure

An accept path will submit validated requests to a fixed worker pool through a bounded queue.
Queue capacity and connection count will be configuration limits. Saturation behavior will be
explicit and measured; there will be no detached threads and no unbounded task accumulation.

### Storage engine

The current engine explicitly serializes append-only records containing type, lengths, sequence,
independent header/payload checksums, key, and value. Writes append and flush before publishing a
new record location. Recovery replays complete valid records, rebuilds the index, truncates a proven
incomplete final record, and fails loudly on complete-record corruption. See `STORAGE_FORMAT.md`.

### Index

The index is planned as independently locked shards. Each shard owns a `std::unordered_map` from
key to immutable record location and protects it with `std::shared_mutex`. GET uses shared access;
PUT and DELETE use exclusive access. Shard count remains configurable because benchmarks, not
assumption, will select defaults.

### Expiration

A min-heap is the initial planned expiration structure. Heap entries will carry enough identity
to recognize stale entries after a key update. Reads must also reject expired values, so scheduler
delay cannot make expired data visible.

### Compaction

Compaction will copy only records that remain current, then durably publish replacement segment
metadata before retiring old files. The detailed synchronization and crash protocol are deferred
until segment/recovery invariants exist and can be tested.

## Ownership model

The server process owns one `TcpServer`, which owns the listening descriptor and `StorageEngine`.
Each accepted descriptor is closed after its connection loop. The application owns and joins the
server thread used for signal-driven shutdown. A future worker pool and work items will remain owned
by the top-level server; raw owning pointers and detached threads remain excluded.

## Planned distributed boundary

Milestones 8 and 9 may add an explicit cluster router, consistent-hash ring, and primary/replica
protocol after single-node correctness and benchmarks. No consensus, leader election,
linearizability, automatic safe failover, or split-brain prevention is implied by that roadmap.
