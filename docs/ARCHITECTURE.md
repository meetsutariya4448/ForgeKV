# Architecture

## Status legend

- **Implemented:** present and exercised in Milestone 0.
- **Planned:** architectural direction that must be validated in its milestone.

## System context

```text
                         planned TCP
┌──────────────┐      ┌──────────────────┐      ┌─────────────────────┐
│ CLI / bench  │─────>│ network server   │─────>│ bounded dispatcher  │
│ skeletons    │      │ [planned M2]     │      │ [planned M3]        │
└──────────────┘      └──────────────────┘      └──────────┬──────────┘
                                                         │
                       ┌─────────────────────────────────┼───────────────┐
                       v                                 v               v
              ┌────────────────┐               ┌────────────────┐ ┌─────────────┐
              │ sharded index  │               │ storage engine │ │ TTL heap    │
              │ [planned M3]   │<─────────────>│ [planned M1]   │ │ [planned M4]│
              └────────────────┘               └───────┬────────┘ └─────────────┘
                                                      v
                                              ┌────────────────┐
                                              │ segment files  │
                                              │ recovery       │
                                              │ compaction M5  │
                                              └────────────────┘
```

Only the shared `forgekv` library, version API, application entry points, build system, and one
unit test are implemented now. Arrows between planned components express intended control/data
flow, not a compatibility promise.

## Planned single-node boundaries

### Protocol and network

The network layer will own listening and connected sockets through RAII wrappers. It will parse a
versioned, length-delimited binary protocol with bounded key/value/frame sizes. Connection code
will handle partial reads, partial writes, timeouts, cancellation, and clean shutdown. Protocol
bytes will not borrow memory beyond the input buffer lifetime.

### Request dispatch and backpressure

An accept path will submit validated requests to a fixed worker pool through a bounded queue.
Queue capacity and connection count will be configuration limits. Saturation behavior will be
explicit and measured; there will be no detached threads and no unbounded task accumulation.

### Storage engine

The first engine will serialize append-only records containing type, lengths, sequence, checksum,
key, and value. Writes will append to a segment before publishing the new record location in the
index. Recovery will replay complete valid records, rebuild the index, and treat a partial final
record according to the storage-format contract written in Milestone 1.

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

The server process will own one top-level application object. That object will own the network
server, worker pool, and storage engine. The storage engine will own segment handles, index, and TTL
state. Work items will own request data until completion. Shared ownership will be introduced only
when asynchronous lifetime crosses a clearly documented boundary; raw owning pointers and detached
threads are excluded.

## Planned distributed boundary

Milestones 8 and 9 may add an explicit cluster router, consistent-hash ring, and primary/replica
protocol after single-node correctness and benchmarks. No consensus, leader election,
linearizability, automatic safe failover, or split-brain prevention is implied by that roadmap.
