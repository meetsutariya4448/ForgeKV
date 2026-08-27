# Replication Protocol and Model

## Placement and acknowledgement

`ReplicatedCluster` accepts RF=1, 2 or 3 when membership has enough nodes. The hash-ring primary is
first and clockwise distinct nodes are replicas. `primary` acknowledgement succeeds after the
selected primary applies the mutation; unavailable replicas may lag. `all` succeeds only after all
placements apply. A failed `all` result is uncertain to the caller because the primary may already
have applied it. There is no quorum mode or automatic retry/deduplication contract.

Sequences are monotonic per `(primary ID, binary key)` stream. This makes a replica distinguish a
duplicate (`sequence <= last`) from a gap (`sequence > last + 1`) without treating unrelated keys as
missing updates. Recovery replays retained stream history to a reachable placed replica; lag is
latest-primary minus replica sequence for that key. Snapshot installation restores latest values
and stream sequence watermarks for restart tests.

## Wire layout

All integers are big-endian and fields are encoded explicitly.

| Offset | Field | Width | Rule |
|---:|---|---:|---|
| 0 | magic | 4 | ASCII `FKRP` |
| 4 | version | 2 | `1` |
| 6 | header bytes | 2 | `48` |
| 8 | operation | 1 | `1=PUT`, `2=DELETE` |
| 9 | flags | 1 | zero |
| 10 | reserved | 2 | zero |
| 12 | stream sequence | 8 | nonzero |
| 20 | absolute expiry ms | 8 | zero means persistent |
| 28 | primary ID bytes | 2 | `1..255` |
| 30 | reserved | 2 | zero |
| 32 | key bytes | 4 | `1..65,536` |
| 36 | value bytes | 4 | `0..16 MiB` |
| 40 | header CRC32C | 4 | bytes `[0,40)` |
| 44 | payload CRC32C | 4 | primary ID + key + value |
| 48 | payload | variable | primary ID, binary key, binary value |

DELETE must have zero value and expiry. Decoding validates all bounds and both checksums before
constructing the message.

## Failure boundary

The current coordinator and endpoints are an in-process deterministic transport model used to
exercise protocol semantics, RF placement, delays, timeouts, unavailable nodes, lag and recovery.
It is not connected to independent server processes or durable replica journals. It therefore does
not demonstrate networked availability, persisted replica recovery, membership consensus, leader
election, safe failover, split-brain prevention, linearizability or quorum consistency.
