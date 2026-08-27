# Storage Format

ForgeKV writes storage format version 2 and can replay version 1 and version 2 records in the same
append-only segment. This document contains enough detail to implement an independent decoder.

## Segment layout

A database contains one or more segments:

```text
segment-00000000000000000001.fkv
segment-00000000000000000002.fkv
```

The identifier is a zero-padded 20-digit decimal number; zero is unused. There is no segment-level
header. Records start at offset zero and are concatenated without alignment or padding. An empty
segment is valid. IDs increase on rotation; the highest ID is active. Gaps are valid after
compaction. `.compact` and `.old` suffixes are publication artifacts described in `COMPACTION.md`.

All multibyte integers are unsigned and big-endian. Fields are encoded individually; native C++
layout, padding, alignment, and endianness are never persisted.

## Version 2 record

The current writer emits a 44-byte header followed by `key_length + value_length` opaque bytes.

| Offset | Field | Width | Encoding and validation |
|---:|---|---:|---|
| 0 | magic | 4 | ASCII `FKVR` (`46 4b 56 52`) |
| 4 | format version | 2 | `uint16`, value `2` |
| 6 | header size | 2 | `uint16`, value `44` |
| 8 | operation | 1 | `1=PUT`, `2=DELETE` |
| 9 | flags | 1 | Zero |
| 10 | reserved | 2 | Zero |
| 12 | sequence | 8 | Nonzero `uint64`; strictly increases physically |
| 20 | key length | 4 | `1..65,536` |
| 24 | value length | 4 | `0..16,777,216`; DELETE requires zero |
| 28 | expires at | 8 | Unix epoch milliseconds; zero means persistent; DELETE requires zero |
| 36 | header CRC32C | 4 | CRC32C over bytes `[0,36)` |
| 40 | payload CRC32C | 4 | CRC32C over key followed by value |
| 44 | key | variable | Opaque binary bytes |
| `44 + key length` | value | variable | Opaque binary bytes; empty PUT value is valid |

The maximum version 2 record is:

```text
44 + 65,536 + 16,777,216 = 16,842,796 bytes
```

An expiration is attached to a PUT record, not stored as a separate mutation. Replaying an expired
PUT removes any older value for that key from the recovered live index. Expiration does not append a
tombstone: the absolute deadline remains authoritative on every later replay.

## Version 1 compatibility

Version 1 records remain readable and are always interpreted as persistent. They use the original
36-byte layout:

| Offset | Field | Width | Encoding and validation |
|---:|---|---:|---|
| 0 | magic | 4 | ASCII `FKVR` |
| 4 | format version | 2 | `uint16`, value `1` |
| 6 | header size | 2 | `uint16`, value `36` |
| 8 | operation | 1 | `1=PUT`, `2=DELETE` |
| 9 | flags | 1 | Zero |
| 10 | reserved | 2 | Zero |
| 12 | sequence | 8 | Nonzero, physically increasing |
| 20 | key length | 4 | `1..65,536` |
| 24 | value length | 4 | `0..16,777,216`; DELETE requires zero |
| 28 | header CRC32C | 4 | CRC32C over bytes `[0,28)` |
| 32 | payload CRC32C | 4 | CRC32C over key followed by value |
| 36 | key then value | variable | Same limits and semantics as above |

A database upgraded in place may therefore contain v1 records followed by v2 records. New writes do
not rewrite existing bytes. Tests cover codec and full-engine mixed-version replay.

## CRC and validation

Both versions use CRC-32C/Castagnoli with reflected polynomial `0x82f63b78`, initial value
`0xffffffff`, and final XOR `0xffffffff`; `123456789` produces `0xe3069283`. CRC detects accidental
corruption, not malicious modification.

The header CRC protects operation, sequence, lengths, and—in v2—the deadline before recovery trusts
them. The payload CRC independently protects the exact key/value bytes. A decoder:

1. Requires at least the smallest complete header (36 bytes) or classifies a final suffix as a
   truncated header.
2. Validates magic, version, and the matching `36`/`44` header size.
3. Requires that full version-specific header and verifies its header CRC.
4. Validates flags, reserved bits, operation, sequence, lengths, and expiration rules.
5. Computes total size with widened arithmetic and enforces the version-specific maximum.
6. Requires the complete payload and verifies its CRC before returning owned key/value bytes.

Unknown versions, mismatched header sizes, impossible lengths, invalid operation metadata, and
complete records with bad checksums are corruption. No decoded field can trigger allocation beyond
the configured key/value limits.

## Record locations and ownership

The sharded index owns every key string. A `RecordLocation` copies segment ID, record/value offsets,
version-specific header size, lengths, sequence, payload checksum, and absolute expiration. GET
copies the location while holding one shard's shared lock. A shared segment-set lock spans location
lookup through the file read so compaction cannot retire that segment. GET checks the payload and
key, rechecks expiration, and returns an owned byte vector. No API result borrows map or file
storage.

## Replay and failure classification

Recovery discovers numeric segment names, walks segment then record order, accepts mixed headers,
requires strictly increasing sequences (gaps are valid), and
applies:

- persistent/unexpired PUT: publish its location and schedule any deadline;
- already-expired PUT: remove the key so it cannot reveal an older version;
- DELETE: remove the key while retaining the tombstone on disk.

At clean EOF of the highest segment, the writer reopens it with `O_APPEND`. EOF before a complete
minimum header, or after a
valid version/header-size prefix but before the rest of that final header/payload, truncates back to
the last complete record. A bad complete header/payload, unknown metadata, or non-increasing sequence
fails open without scanning forward or modifying the segment. A truncated tail in an inactive
segment is corruption because rotation closes complete records before advancing.

## Durability modes

All modes issue bounded POSIX `write` loops before publishing a location. They differ in stable-
storage synchronization:

| Mode | Acknowledgement boundary | Clean close | Crash/power-loss window |
|---|---|---|---|
| `always` | `fsync(segment)` succeeds; required new file/directory entries are synced | Already synced mutations; outstanding repair metadata is synced | An acknowledged single record is expected to survive under normal filesystem/fsync semantics |
| `periodic` | After `write`, before `fsync` | Dirty segment and new directory entries are synced | Acknowledged writes since the last successful periodic sync may be lost; scheduling/fsync delay means the interval is not a hard bound |
| `none` | After `write`; no `fsync` | Closes without `fsync` | Any page-cache-only acknowledged data may be lost on kernel/power failure |

`periodic` is the current default with a one-second interval. Its background thread and clean close
serialize `fsync` with appends through the writer mutex. `always` syncs the segment before in-memory
publication. When ForgeKV creates the database leaf and first segment, durable modes also sync the
database directory and its immediate parent; callers using a path with multiple simultaneously
missing ancestors should pre-create that hierarchy if they require every ancestor entry to have an
explicit sync boundary.

The abrupt-exit integration test verifies that an `always` acknowledgement can be replayed after a
child calls `_exit` without destructors. It is not a simulated power failure or proof about every
filesystem/hardware stack. ForgeKV still has no transactions, batches, rollback, multi-record
atomicity, process-level database lock, or guarantee for filesystems that do not honor ordinary
`fsync` semantics.
