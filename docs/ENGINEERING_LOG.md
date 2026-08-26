# Engineering Log

Only problems actually observed during development belong here.

## 2026-08-26: CMake absent from the local toolchain

- **Problem:** The required CMake configure command could not run from the initial environment.
- **Symptoms:** `cmake` was not found on `PATH`; Ninja, clang-format, and clang-tidy were also absent.
- **Root cause:** The macOS developer toolchain supplies Apple Clang and Git but no CMake package.
- **Fix:** Used an isolated CMake Python package under the Codex workspace for this milestone. No
  system-wide package was installed and no tool binary was added to the repository.
- **Test added:** None; this is an environment/tooling issue.
- **Result:** Configure, build, and test completed using the local tool. A user-managed CMake
  installation remains the cleaner long-term developer setup.

## 2026-08-26: Auto-discovered GoogleTest had the wrong CPU architecture

- **Problem:** The first normal build compiled project sources but could not link the unit test.
- **Symptoms:** The linker ignored `/opt/anaconda3/lib/libgtest*.dylib` because those libraries were
  `x86_64` while ForgeKV and the host are `arm64`, followed by undefined GoogleTest symbols.
- **Root cause:** An unconditional `find_package` accepted an unrelated Anaconda GoogleTest 1.11
  package without guaranteeing target-architecture compatibility.
- **Fix:** Made pinned FetchContent GoogleTest v1.15.2 the default. Using a system package now
  requires explicit `FORGEKV_USE_SYSTEM_GTEST=ON` opt-in.
- **Test added:** The clean configure/build/test verification exercises the default dependency path.
- **Result:** The repeated clean configure, build, and test passed with the pinned arm64-compatible
  GoogleTest build.

## 2026-08-26: AddressSanitizer binaries hang at process startup

- **Problem:** ASan-instrumented binaries cannot complete startup in the inspected environment.
- **Symptoms:** GoogleTest discovery timed out after linking, and the much smaller
  `forgekv-server` skeleton also failed to exit within eight seconds. Setting
  `ASAN_OPTIONS=detect_leaks=0` did not change the result.
- **Root cause:** Not established. The behavior occurs before project logic runs and also appeared
  in the initial standalone compiler probe, so current evidence points to the local Apple
  Clang/macOS sanitizer runtime rather than ForgeKV code.
- **Fix:** Moved GoogleTest discovery to CTest time so instrumentation builds do not execute target
  binaries as a link side effect. ASan support remains configured, but this environment must not be
  reported as an ASan test pass.
- **Test added:** A separate UBSan-only build/test verifies the sanitizer wiring that this host can
  execute. ASan should be rerun on a supported Linux/Clang CI runner in a later milestone.
- **Result:** UBSan passed. ASan runtime remains unavailable locally. A separate TSan-instrumented
  build succeeded, but its test process segfaulted during discovery before project test logic ran;
  TSan is therefore also not reported as passing on this host.
