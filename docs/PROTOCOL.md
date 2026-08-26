# ForgeKV Protocol Version 1

ForgeKV uses a length-delimited binary protocol directly over TCP. TCP is a byte stream: one frame
may arrive across many reads, and one read may contain several frames. Implementations must not rely
on packet boundaries or textual delimiters.

## Byte order and frame layout

All multibyte integers are unsigned and big-endian. Fields are encoded explicitly; no C++ struct
layout is transmitted. Every request and response has a 40-byte header followed by key bytes and
value bytes.

| Offset | Field | Width | Encoding and validation |
|---:|---|---:|---|
| 0 | magic | 4 | ASCII `FKVP` (`46 4b 56 50`) |
| 4 | version | 2 | `uint16`, value `1` |
| 6 | header size | 2 | `uint16`, value `40` |
| 8 | kind | 1 | `1=request`, `2=response` |
| 9 | opcode | 1 | `1=PUT`, `2=GET`, `3=DELETE`, `4=EXISTS` |
| 10 | status | 2 | Status table below; requests use `0` |
| 12 | flags | 2 | Zero in version 1 |
| 14 | reserved | 2 | Zero in version 1 |
| 16 | request ID | 8 | Nonzero `uint64`; response echoes request |
| 24 | key length | 4 | `0..65,536`; requests semantically require nonempty key |
| 28 | value length | 4 | `0..16,777,216` |
| 32 | header CRC32C | 4 | CRC32C over bytes `[0, 32)` |
| 36 | payload CRC32C | 4 | CRC32C over key bytes followed by value bytes |
| 40 | key | variable | Opaque binary bytes |
| `40 + key length` | value | variable | Opaque binary bytes |

Maximum frame size is `40 + 65,536 + 16,777,216 = 16,842,792` bytes. Lengths are validated with
widened checked arithmetic before buffering the declared payload. CRC32C uses the same Castagnoli
convention specified in `STORAGE_FORMAT.md`; it detects accidental corruption, not malicious input.

## Request semantics

| Opcode | Key | Value |
|---|---|---|
| PUT | Required | `0..16 MiB`; empty value is valid |
| GET | Required | Must be empty |
| DELETE | Required | Must be empty |
| EXISTS | Required | Must be empty |

Structurally valid frames with invalid operation semantics receive `INVALID_REQUEST`; the
connection remains usable. Unknown opcode/kind, invalid checksum, bad magic/version/header size,
zero request ID, unsupported flags, or excessive lengths are malformed framing and close the
connection without a response. The server does not trust a request ID from a malformed header.

## Responses

Responses use kind `2`, echo opcode and request ID, and currently have an empty key. Status codes:

| Value | Name | Meaning |
|---:|---|---|
| 0 | OK | Operation completed |
| 1 | NOT_FOUND | GET or DELETE key was absent |
| 2 | INVALID_REQUEST | Frame was structurally valid but semantically invalid |
| 3 | INTERNAL_ERROR | Unexpected server failure |
| 4 | STORAGE_ERROR | Storage operation failed |
| 5 | OVERLOADED | Valid request was rejected because the worker queue was full or stopping |

Payloads by operation/status:

- PUT/OK and DELETE/OK: empty value.
- GET/OK: stored value bytes, including a possible empty value.
- GET/NOT_FOUND and DELETE/NOT_FOUND: empty value.
- EXISTS/OK: exactly one byte, `00=false` or `01=true`.
- Error status: bounded diagnostic text may be placed in the value. Clients must use status, not
  diagnostic wording, for control flow.

Request IDs correlate responses; they are not transactions, deduplication keys, or replay
protection. Each connection dispatches one request at a time and therefore responds in request order.
Pipelined frames are parsed but executed sequentially on that connection; the current client sends
one request and waits for its response.

## Incremental parser

```text
TCP bytes
   |
   v
buffer until 40 bytes
   |
validate magic/version/header CRC/lengths
   |
buffer only the bounded declared payload
   |
validate payload CRC
   |
emit one frame, reset, continue with remaining bytes
```

The parser buffers at most one maximum-sized frame. It supports every fragmentation boundary and
multiple complete frames in one input span. After any protocol error, that parser instance is
poisoned and cannot be reused; the server closes the associated connection rather than attempting
resynchronization.

## Socket behavior

The POSIX socket layer owns descriptors through `TcpServer`/`TcpClient` destructors. Send loops
continue after partial writes and `EINTR`; receive loops accept arbitrary partial reads. SIGPIPE is
suppressed. Accepted sockets use bounded receive/send timeouts so the server can observe stop
requests and a client cannot block one I/O call indefinitely. EOF is a clean disconnect.

The listening loop uses `poll` with a bounded interval for cancellation. It admits at most the
configured connection count, with one owned `std::jthread` per active connection. An excess accepted
socket is closed immediately without a response because no request has been parsed. Valid requests
are submitted without blocking to a bounded worker queue; a full or stopping queue receives
`OVERLOADED` and the connection remains usable. These two saturation behaviors are intentionally
different and deterministic.

Client response-read timeouts are enforced. The current blocking `connect()` path does not provide a
strict cross-platform connection-attempt deadline; that remains a documented limitation.

## Forward compatibility

Version and header size are explicit. Version 1 rejects unknown versions, header sizes, flags,
kinds, opcodes, and statuses rather than guessing. A future version may define new fields or
operations, but negotiation and mixed-version compatibility are not implemented.

## Example exchange

```text
client                      server/storage
  | PUT id=1 key K value V       |
  |----------------------------->| append PUT, update index
  |       OK id=1                |
  |<-----------------------------|
  | GET id=2 key K               |
  |----------------------------->| read and checksum value
  |       OK id=2 value V        |
  |<-----------------------------|
```
