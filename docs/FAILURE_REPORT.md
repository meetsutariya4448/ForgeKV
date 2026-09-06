# Benchmark and Failure Report

## Evidence captured

- Current Linux Release suite: 122/122 tests pass, including process ownership, network failure,
  overload and background compaction coverage.
- Current quick TCP matrix: 42/42 trials valid across 14 variants, with zero reported operation or
  connection errors; raw JSON/CSV/latencies, logs, resources and distribution summary are under
  `bench/raw/matrix-20260905T214247Z-*`.
- Five-second resource-profile workload: 263,044 operations, zero reported errors, 52,595.6
  operations/s, 682.917 us p50 and 1,692.17 us p99; raw output is under
  `bench/raw/m10-profile-20260826*`.
- Abrupt writer `_exit`: an `always`-acknowledged record recovered on restart.
- Process ownership: an independent second writer is rejected while the first holds the database;
  after the holder is killed, a new engine acquires ownership and recovers normally.
- Truncated active tails recover preceding records; complete checksum corruption and inactive-
  segment truncation fail loudly.
- Compaction tests cover pre-publication rollback, post-publication cleanup, concurrent mutation,
  byte reduction and restart.
- Network tests cover malformed frames, overload, excess connections, idle shutdown, refusal,
  response timeout and reset.
- Linux slow-client scenario: 32 incomplete-frame clients produced 39 total threads and 38 open
  descriptors from baselines of 7 and 6; all 16 excess requests were rejected, and both resources
  returned to baseline after clients closed.
- Replication-model tests cover unavailable primary/replica, slow-replica timeout, ordering gap,
  duplicates, snapshot restart, lag and history recovery.

## Recovery interpretation

The `_exit` test bypasses destructors but is not a power-cut or controller-cache test. Compaction
boundary tests construct every recognized on-disk state rather than killing at instruction-exact
points. Timeout assertions establish bounded behavior in the loopback test environment, not a WAN
service-level objective. The in-process replication failure model validates state-machine rules,
not an actual multi-host failure detector.

## Performance interpretation

The quick matrix is a functional experiment. It is too short and has too few repetitions for
comparative claims. No result is deleted for looking unexpected: notably the observed `always`,
`periodic`, and `none` ordering is retained and classified as noisy. The full script and raw-output
contract make a later controlled report reproducible.

The bounded resource capture is preserved in `bench/reports/m10-server-resource-20260826.txt` and
`bench/reports/m10-server-sample-20260826.txt`. BSD `time -l` reported a 2,490,368-byte maximum
resident set size, 1,982,776-byte peak memory footprint and zero swaps. The measured 26.42-second
server lifetime includes setup and idle time. The three-second stack sample found maintenance
threads mostly waiting and sampled periodic fsync activity; it is diagnostic evidence, not a CPU
attribution study or capacity claim.

The newer Linux task-clock `perf`/flame-graph and syscall capture is preserved under
`bench/raw/profile-read-heavy-tcp-nodelay-final-20260905T221320Z-6143f5a79676/`. A five-trial
alternating comparison under `bench/raw/profile-read-heavy-tcp-nodelay-comparison-*` measured a
93.07x median throughput increase and 97.99% lower median p99 batch latency after enabling
`TCP_NODELAY`, with zero reported errors. The workload used `durability=none`, a dirty working tree,
loopback networking and Docker Desktop's Linux VM, so it supports this narrow comparative claim—not
absolute capacity, native-hardware, or durability claims.

## Sanitizer boundary

For commit `6143f5a796760b818e30f907a3f2dd87373b7d3c`, GitHub Actions run `33884876502`
completed successfully on Ubuntu 24.04. ASan+UBSan and TSan each passed 121/121 tests. The record
decoder and frame parser libFuzzer targets each completed 10,000 runs. These results describe that
named commit. The final uncommitted development tree was also rebuilt in an Ubuntu 24.04 container
with Clang 18.1.3: ASan+UBSan and TSan each passed 122/122 tests, and both fuzz targets completed
10,000 runs. That local container result is evidence for the working tree, not a named CI commit.
