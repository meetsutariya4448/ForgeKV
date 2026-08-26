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

## Pending decisions for later milestones

Protocol framing, record layout, checksum, durability defaults, maximum sizes, sequence publication,
index shard default, TTL clock semantics, segment rotation, and compaction publication are not
decided by the foundation milestone. Each will receive a decision record alongside its executable
specification and tests.
