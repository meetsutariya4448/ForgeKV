# Segment Rotation and Compaction

## Rotation

`segment_max_bytes` is a soft per-segment target. Before appending a record, the mutation mutex
checks whether the nonempty active segment plus that record would exceed the target. If so, durable
modes synchronize the old descriptor, create the next monotonically numbered segment, publish the
new active descriptor, and fsync the database directory with the next durability synchronization.
A record larger than the target is allowed alone so the storage record limit remains independent of
the operational rotation target.

Recovery discovers exact `segment-%020llu.fkv` names, sorts numeric IDs, and replays them in order.
Only the highest-ID active segment may have a repairable truncated tail; truncation in a closed
segment is corruption.

## Concurrent compaction

The compactor selects all inactive segments. It snapshots the sharded index and copies only current,
unexpired locations from those candidates into a v2 replacement, sorted by sequence. Copying and
checksumming happen while writers continue against the active segment. Publication then takes the
mutation mutex and the exclusive segment-set lock for a short rename/index-update window.

A GET holds the shared segment-set lock from index lookup through file read. Therefore it sees
either the old location and old file set or the replacement location and replacement file set. Each
index update is conditional on the original sequence; a write that raced with copying wins and its
active-segment location is never overwritten by stale compaction output.

Metrics report candidate count, input bytes, replacement bytes, bytes written and elapsed time.
`bytes_after / bytes_before` is the retained-byte ratio; compaction write amplification for that run
is `bytes_written / logical_live_bytes` (currently 1 because output is written once).

## Publication and crash cleanup

For candidate IDs `A..N`, the protocol is:

1. Write `segment-A.fkv.compact`; fsync it in `always` and `periodic` modes.
2. Rename each candidate to `segment-ID.fkv.old`; fsync the directory in durable modes.
3. Rename the compact file to `segment-A.fkv`; this is the observable commit point; fsync the
   directory in durable modes.
4. Conditionally republish live index locations.
5. Delete `.old` files and fsync the directory in durable modes.

At startup, `.old` files with no replacement `segment-A.fkv` mean publication did not commit, so all
old names are restored and the compact file is removed. If the replacement exists, publication won
and old files are deleted. A lone `.compact` file is abandoned pre-publication output and is
removed. Tests construct both recovery states directly.

This protocol assumes atomic same-filesystem rename and POSIX unlink semantics. `none` mode does not
claim crash durability for compaction metadata. Directory fsync behavior still depends on the
filesystem and storage stack.
