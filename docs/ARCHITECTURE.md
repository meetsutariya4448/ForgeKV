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
              │ [impl. M3/M4]  │<─────────────>│ [impl. M1-M4]  │ │ [impl. M4]  │
              └────────────────┘               └───────┬────────┘ └─────────────┘
                                                      v
                                              ┌────────────────┐
                                              │ segment files  │
                                              │ recovery       │
                                              │ compaction     │
                                              └────────────────┘
```

The shared library, rotating/compacting storage engine, TTL scheduler, sharded index, versioned
network codec, bounded concurrent POSIX server/client, worker pool, CLI, observability and pipelined
TCP benchmark are implemented. The same library contains cluster routing and replication state
machines that are intentionally separate from the single-node executable.

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

The engine uses a global mutation mutex to serialize the active append stream, sequence, and fsync.
Version 2 records add an absolute expiration to type, lengths, sequence, checksums, key, and value;
mixed v1/v2 replay preserves older databases. Writes use POSIX append loops and the selected
`always`/`periodic`/`none` synchronization boundary before or after publication as documented.
Concurrent reads copy a location and then read the append-only segment without holding an index
lock. Recovery rebuilds the index, filters expired values, truncates a proven incomplete final
record, and fails loudly on complete-record corruption. See `STORAGE_FORMAT.md`.

### Index

The index consists of independently locked shards. Each owns a `std::unordered_map` from an owned key
to a copied record location and protects it with `std::shared_mutex`. GET uses shared access; PUT and
DELETE use exclusive access for one shard. No operation holds two shard locks. Shard count is
configurable; the provisional default is 16 and its evidence boundary is documented in
`CONCURRENCY.md`.

### Expiration

One maintenance `std::jthread` owns no data directly; it waits on a mutex-protected min-heap of
absolute deadlines. Entries carry key and sequence so an old deadline cannot delete an overwritten
value. PUTEX inserts in `O(log n)`, expiration pops in `O(log n)`, and TTL/GET use average `O(1)`
index lookup. GET checks expiration before and after file I/O, so scheduler or wall-clock wakeup delay
does not expose an expired value. Expiration is logical and does not append tombstones.

### Rotation and compaction

The active log rotates at a configurable soft byte target. Compaction snapshots current locations,
copies live records from inactive segments while writes continue, then briefly takes mutation and
segment-set publication locks. Conditional sequence replacement prevents a stale copy from winning
over a concurrent write. GET holds the shared segment-set lock from lookup through read so old files
cannot disappear under a copied location. Rename artifacts make interrupted publication detectable;
see `COMPACTION.md`.

## Ownership model

The server process owns one `TcpServer`, which owns the listener, `StorageEngine`, `WorkerPool`,
accepted connection threads, and their descriptors. The pool owns worker threads and queued tasks.
The storage engine owns periodic-sync, expiration and compaction `std::jthread`s plus its active
segment descriptor.
The application owns and joins the server thread used for signal-driven shutdown. Shutdown joins
connections, drains workers, stops storage maintenance, applies the configured final sync, and closes
the segment. Raw owning pointers and detached threads remain excluded.

## Distributed library boundary

```text
binary key -> consistent-hash ring -> primary + clockwise distinct replicas
                                      |
                                      v
                         replication message/state model
                         sequence gap + lag + recovery
```

The deterministic ring and replication protocol/model are implemented and tested as library
components. They are not wired to `forgekv-server` or independent processes. No consensus, leader
election, linearizability, automatic safe failover, split-brain prevention, durable replica journal,
or membership control plane is implied. See `CLUSTER.md` and `REPLICATION.md`.
