# Storage Format

This document specifies ForgeKV storage format version 1. An independent implementation should be
able to decode and validate a Milestone 1 segment using only this document.

## Segment files

A database is a directory. Milestone 1 uses exactly one append-only segment:

```text
segment-00000000000000000001.fkv
```

The decimal segment identifier is zero-padded to 20 digits. Segment ID zero is not used. A new
database contains an empty segment, which is valid. There is no segment-level header in format v1;
the first record begins at byte zero. Segment rotation and multiple-segment recovery are not yet
implemented, but numeric IDs and record locations preserve the necessary identity.

## Primitive encoding

All multibyte integers are unsigned and encoded in network byte order (big-endian). Records are
encoded field by field. Raw C++ structs, native byte order, padding, and alignment are never written
to disk.

## Version 1 record

Each record is a fixed 36-byte header followed immediately by `key_length + value_length` payload
bytes. Key and value bytes are opaque; no text encoding or terminator is implied.

| Offset | Field | Width | Encoding | Validation |
|---:|---|---:|---|---|
| 0 | magic | 4 | ASCII bytes `46 4b 56 52` (`FKVR`) | Exact match |
| 4 | format version | 2 | big-endian `uint16`, value `1` | Other values are unsupported |
| 6 | header size | 2 | big-endian `uint16`, value `36` | Other values are unsupported in v1 |
| 8 | operation | 1 | `uint8` | `1=PUT`, `2=DELETE` |
| 9 | flags | 1 | `uint8`, value `0` | Nonzero is unsupported |
| 10 | reserved | 2 | big-endian `uint16`, value `0` | Nonzero is corruption/unsupported |
| 12 | sequence | 8 | big-endian `uint64` | Nonzero; strictly increases physically |
| 20 | key length | 4 | big-endian `uint32` | `1..65,536` bytes |
| 24 | value length | 4 | big-endian `uint32` | `0..16,777,216` bytes; DELETE requires `0` |
| 28 | header CRC32C | 4 | big-endian `uint32` | CRC32C of bytes `[0, 28)` |
| 32 | payload CRC32C | 4 | big-endian `uint32` | CRC32C of bytes `[36, record_end)` |
| 36 | key | `key length` | opaque bytes | Non-empty |
| `36 + key length` | value | `value length` | opaque bytes | Empty allowed for PUT |

The maximum encoded record is:

```text
36 + 65,536 + 16,777,216 = 16,842,788 bytes
```

The CRC convention is CRC-32C/Castagnoli with reflected polynomial `0x82f63b78`, initial value
`0xffffffff`, and final XOR `0xffffffff`. The standard check input `123456789` produces
`0xe3069283`. The payload checksum of an empty payload is zero.

### Why two checksums

The header checksum protects lengths, operation, and sequence independently of the payload. After a
complete valid header, recovery can safely decide that a payload extending beyond EOF is an
incomplete final append. The payload checksum detects corruption in a complete record. A single
checksum over header plus payload cannot validate length metadata when the payload is missing, so it
cannot distinguish a plausible truncated append from a damaged length field as confidently.

The checksums detect accidental corruption; they do not authenticate data and provide no protection
against intentional modification with recomputed checksums.

## Record validation order

A decoder performs the following bounded steps:

1. Require 36 header bytes before reading any field.
2. Validate magic, version, and header size.
3. Recompute the header CRC32C over bytes `[0, 28)`.
4. Validate flags, reserved bits, operation, and nonzero sequence.
5. Validate key/value policy and implementation limits.
6. Compute `36 + key_length + value_length` using widened, checked arithmetic.
7. Require the complete payload before allocating/copying decoded fields.
8. Recompute payload CRC32C over the exact key-plus-value payload.

No decoded length can request more than 16,842,788 bytes. Values read from disk remain fixed-width
until validation; narrowing to `size_t` occurs only after representability and configured limits are
checked.

## Sequence and state semantics

Sequences begin at one. A successful append consumes exactly one sequence. Gaps are allowed for
future compatibility, but zero, duplicate, and decreasing sequences are corruption. On recovery,
records are applied in physical order:

- PUT inserts or replaces the key's `RecordLocation`.
- DELETE removes the key from the live index. The tombstone remains in the segment.
- Repeated PUTs therefore resolve to the highest later valid sequence.
- PUT followed by DELETE remains absent after restart.

Recovery sets the next mutation sequence to one greater than the largest replayed sequence. If the
largest sequence is `UINT64_MAX`, reads remain possible but further mutation is rejected.

## Record locations and ownership

The in-memory index owns each key as a binary-safe `std::string`. Each value is represented by:

```text
RecordLocation
├── segment_id
├── record_offset
├── value_offset
├── key_length
├── value_length
├── sequence
└── payload_checksum
```

The `StorageEngine` solely owns the index and active append stream. GET opens a bounded read stream,
loads the key-plus-value payload, rechecks its checksum and key identity, and returns an owned byte
vector. No returned view borrows segment or index memory. This is intentionally single-threaded in
Milestone 1.

The segment ID and offsets allow later rotation/compaction to publish replacement locations without
changing key ownership or the public value-return contract.

## Recovery algorithm

On open, ForgeKV creates a missing database directory and empty initial segment, then:

1. Read the segment's file size once.
2. Starting at offset zero, require a complete 36-byte header.
3. Decode and validate the header, including its checksum and bounded total size.
4. If the validated record extends beyond EOF, classify it as an incomplete final payload.
5. Otherwise read the bounded payload, validate its checksum, and require a sequence greater than
   the previous record.
6. Apply PUT/DELETE to the index and advance to the exact next-record offset.
7. At clean EOF, open the segment in append mode.
8. For an incomplete final header or payload, resize the segment back to the last valid record
   boundary before reopening it for append.

Recovery does not scan for a later magic value or resynchronize after corruption. Doing so could
silently skip state-changing records.

## Failure classification

| Condition | Result |
|---|---|
| Empty segment | Open successfully with empty index |
| All records complete and valid | Replay all records |
| EOF within final 36-byte header | Preserve prior records; truncate incomplete suffix |
| Valid header whose declared payload reaches beyond EOF | Preserve prior records; truncate suffix |
| Bad magic/version/header size | Fail open with `CorruptionError` |
| Bad header CRC, including final record | Fail open with `CorruptionError` |
| Complete record with bad payload CRC | Fail open with `CorruptionError` |
| Unknown operation/flags/reserved bits | Fail open with `CorruptionError` |
| Zero, duplicate, or decreasing sequence | Fail open with `CorruptionError` |
| Corruption followed by later bytes | Fail at the corrupt record; do not modify the segment |

A short final payload is classified as truncation only after a complete, checksum-valid header has
proved that its length metadata is intact. A bit-for-bit complete record with invalid content is
never downgraded to truncation merely because it is last.

## Write and crash semantics

PUT and DELETE encode one complete record, append it, and call C++ stream `flush()` before updating
the live index. The append-only segment is authoritative. If in-memory publication fails after an
append, the engine becomes unavailable until reopen/replay so a sequence cannot be reused.

Milestone 1 does **not** call `fsync`, `fdatasync`, or directory sync. `flush()` transfers bytes from
the C++ stream buffer to the operating system but does not guarantee stable storage. Consequently:

- Clean close/restart is supported and tested.
- A process crash may leave a partial final record; valid earlier records are recovered.
- A power failure or kernel crash may lose recently acknowledged records.
- A torn complete record may cause conservative recovery failure rather than automatic data loss.
- There are no transactions, batches, rollback, or multi-record atomicity guarantees.
- Opening the same database from multiple processes is unsupported and unprotected.

Durability modes and explicit sync policy belong to Milestone 4.

## Forward compatibility

Format version and header size permit a future decoder to dispatch to a new layout. Unknown values
currently fail loudly. Numeric segment IDs and `RecordLocation` segment identity support rotation.
Sequence-preserving records can be copied during compaction, but the multi-segment ordering,
manifest/publication protocol, crash-safe file replacement, and old-segment deletion rules remain
undefined until Milestone 5.
