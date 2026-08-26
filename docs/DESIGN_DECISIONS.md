# Design Decisions

This is an append-only decision log. Evidence fields stay `pending` until a test or benchmark
actually exists.

## DD-001: Use C++20 as the baseline

- **Context:** The project needs modern ownership, concurrency, and lifetime tools while remaining
  practical on common internship/recruiting environments.
- **Alternatives:** C++17; C++23.
- **Chosen approach:** Require C++20 without compiler-specific language extensions.
- **Reason:** C++20 provides `std::jthread`, stop tokens, `std::span`, and improved standard-library
  facilities while having broader support than C++23.
- **Tradeoffs:** Some C++23 conveniences are unavailable; older compilers cannot build the project.
- **Evidence/benchmark:** Milestone 0 builds with Apple Clang 17. Cross-compiler CI is future work.

## DD-002: Build applications on one reusable library

- **Context:** Server, CLI tooling, benchmarks, tests, and future fuzz targets need shared behavior.
- **Alternatives:** Put implementation directly into each executable; create many libraries now.
- **Chosen approach:** Start with one `forgekv` library and three thin executable targets.
- **Reason:** It establishes testable boundaries without inventing subsystem libraries before their
  interfaces exist.
- **Tradeoffs:** The library may be split later as dependency boundaries become measurable.
- **Evidence/benchmark:** All Milestone 0 applications and the unit test link the library.

## DD-003: Configure tests with pinned GoogleTest

- **Context:** Milestone 0 requires GoogleTest, but the local machine does not provide it.
- **Alternatives:** Vendor source; require a system package; use a bespoke test harness.
- **Chosen approach:** FetchContent GoogleTest `v1.15.2` at exact commit
  `b514bdc898e2951020cbdca1304b75f5950d1f59` into the build tree by default. Permit a system package
  only through explicit `FORGEKV_USE_SYSTEM_GTEST=ON` opt-in.
- **Reason:** The version is reproducible, no system-wide install is required, and GoogleTest is
  isolated to test builds.
- **Tradeoffs:** A first clean default test build requires network access. The explicit system mode
  shifts architecture/version compatibility responsibility to the developer.
- **Evidence/benchmark:** Verified by the Milestone 0 configure/build/test run.

## DD-004: Keep sanitizer builds explicit and separate

- **Context:** Sanitizers change generated code and ThreadSanitizer is incompatible with combining
  AddressSanitizer in the same process.
- **Alternatives:** Always enable sanitizers; expose one free-form flags variable.
- **Chosen approach:** CMake options for ASan, UBSan, and TSan, with TSan rejected when combined
  with ASan/UBSan.
- **Reason:** Named configurations are reproducible and fail early for an invalid combination.
- **Tradeoffs:** Sanitizer coverage requires additional build trees and platform support.
- **Evidence/benchmark:** ASan/UBSan verification is recorded for Milestone 0; TSan is evaluated
  separately.

## DD-005: Use a fixed 36-byte, big-endian versioned record header

- **Context:** Recovery must be portable, bounded, independently implementable, and extensible.
- **Alternatives:** Raw C++ structs; native-endian fields; variable-length integers; a fixed header
  without a size/version field.
- **Chosen approach:** Encode every field explicitly in big-endian order with `FKVR` magic, version,
  header size, operation, sequence, lengths, and two checksums.
- **Reason:** Fixed offsets simplify defensive decoding and fuzzing. Explicit byte order avoids ABI,
  padding, alignment, and host-endian dependencies. Version and header size make incompatibility
  visible rather than ambiguous.
- **Tradeoffs:** The 36-byte header has more overhead for small values than a variable-length format.
- **Evidence/benchmark:** Round-trip, boundary, malformed-field, and recovery tests pass. Space
  overhead has not yet been benchmarked.

## DD-006: Use portable CRC32C for header and payload checksums

- **Context:** ForgeKV needs accidental-corruption detection, not cryptographic integrity, and must
  distinguish validated length metadata from an incomplete payload.
- **Alternatives:** IEEE CRC32; xxHash; a cryptographic hash; one checksum over the full record.
- **Chosen approach:** A dependency-free software CRC32C for bytes `0..27` and a separate CRC32C for
  key-plus-value payload bytes.
- **Reason:** CRC32C has storage-oriented burst-error properties and a standard check value; hardware
  acceleration can be added later behind the same format. Two checksums let recovery validate
  lengths before classifying missing payload bytes as a truncated append. xxHash prioritizes speed
  but is not a CRC and would add implementation/dependency surface; cryptographic integrity is out
  of scope.
- **Tradeoffs:** The bitwise implementation is intentionally simple and slower than table or hardware
  variants. Two fields cost four more bytes than a single checksum.
- **Evidence/benchmark:** The `123456789 -> 0xe3069283` check vector and corruption tests pass.

## DD-007: Bound records and reject empty keys

- **Context:** Disk lengths are untrusted and later network input must not create unbounded
  allocation.
- **Alternatives:** Accept the full `uint32` range; make limits configuration-dependent immediately;
  permit empty keys.
- **Chosen approach:** Keys are 1..64 KiB, values are 0..16 MiB, and the maximum record is
  16,842,788 bytes. PUT may store an empty value; DELETE must not contain one.
- **Reason:** Fixed conservative limits make checked allocation and protocol alignment simple. Empty
  keys add little practical value and are rejected consistently at codec and engine boundaries.
- **Tradeoffs:** Larger objects and empty keys require a future format/API decision.
- **Evidence/benchmark:** Exact-limit, over-limit, empty-key, and impossible-length tests pass.

## DD-008: Treat the append-only segment as authoritative

- **Context:** A mutation must not become visible before its record exists, and allocation failure
  after append must not permit sequence reuse.
- **Alternatives:** Update the index first and roll back on write error; keep full values in memory;
  append and continue after an index-publication exception.
- **Chosen approach:** Prepare owned inputs, append and flush, advance sequence, then publish a
  `RecordLocation`. If publication throws after append, reject further operations until reopen.
- **Reason:** Replay can always reconstruct the authoritative state, and a record sequence is never
  reused after its bytes enter the log.
- **Tradeoffs:** `flush()` per operation is slow and still not an `fsync`; an allocation failure can
  require reopening the engine.
- **Evidence/benchmark:** CRUD/restart tests pass. Allocation-failure injection is not implemented.

## DD-009: Repair only checksum-provable truncated tails

- **Context:** Crash truncation and arbitrary corruption must not be silently conflated.
- **Alternatives:** Ignore every invalid final record; scan forward for the next magic; fail on every
  partial append.
- **Chosen approach:** Truncate only a partial final header or a payload missing after a complete,
  checksum-valid header. All complete-record checksum/semantic errors fail recovery without file
  modification.
- **Reason:** This recovers common append interruption while refusing to guess across corrupted
  state-changing records.
- **Tradeoffs:** A torn header that reaches 36 bytes but has a bad checksum causes conservative open
  failure and may require an offline repair tool later.
- **Evidence/benchmark:** Every partial-header boundary, partial payload, complete-final corruption,
  and mid-file corruption behavior is tested.

## Pending decisions for later milestones

Protocol framing, durability modes, index shard default, TTL clock semantics, segment rotation, and
compaction publication are not decided yet. Each will receive a decision record alongside its
executable specification and tests.
