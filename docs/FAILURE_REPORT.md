# Benchmark and Failure Report

## Evidence captured

- Normal local suite: 98/98 tests pass, including the final network-failure and background-
  compaction additions.
- Quick TCP matrix: 14/14 cases valid, 28,000 measured operations, zero reported operation or
  connection errors; raw JSON/CSV/latencies and logs are under `bench/raw/matrix-20260826T235917Z-*`.
- Five-second resource-profile workload: 263,044 operations, zero reported errors, 52,595.6
  operations/s, 682.917 us p50 and 1,692.17 us p99; raw output is under
  `bench/raw/m10-profile-20260826*`.
- Abrupt writer `_exit`: an `always`-acknowledged record recovered on restart.
- Truncated active tails recover preceding records; complete checksum corruption and inactive-
  segment truncation fail loudly.
- Compaction tests cover pre-publication rollback, post-publication cleanup, concurrent mutation,
  byte reduction and restart.
- Network tests cover malformed frames, overload, excess connections, idle shutdown, refusal,
  response timeout and reset.
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

## Sanitizer boundary

UBSan runs locally. ASan and TSan instrumented binaries build on this macOS host, but ASan stalls and
TSan faults before GoogleTest discovery in the local runtime. Linux GitHub Actions jobs are defined
for ASan+UBSan, TSan and libFuzzer smoke runs. Those workflows have not been observed on a remote
commit, so this repository does not claim that CI or TSan passes.
