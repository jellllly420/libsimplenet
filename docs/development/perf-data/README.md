# Performance Artifact Index

## Current Baseline Artifact

- `2026-02-19-suite-release-v5.csv`
  - Suite format: `v4` (schema version in file header), baseline run label: `v5`
  - Includes async fairness comparisons and post-optimization measurements:
    - `libsimplenet` backend `epoll` vs `boost_asio` backend `epoll`
    - `libsimplenet` backend `io_uring` vs `boost_asio` backend `epoll`
      (when `io_uring` is available)
  - Includes metadata rows:
    - `PERF_META`
    - `PERF_META_ASYNC`
    - `PERF_SKIP` (when backend unavailable)

## Async-Integrated Artifact Path (Prior Baseline)

- `2026-02-19-suite-release-v4.csv`
  - Suite format: `v4`
  - Adds async comparison scenarios with pairwise-alternating fairness:
    - `libsimplenet` backend `epoll` vs `boost_asio` backend `epoll`
    - `libsimplenet` backend `io_uring` vs `boost_asio` backend `epoll`
      (when `io_uring` is available)
  - Adds async metadata rows (`PERF_META_ASYNC`) and availability skip rows
    (`PERF_SKIP`) for machine parsing.

## Historical Artifacts (Superseded)

- `2026-02-19-suite-release.csv` (`v1`)
  - Superseded by `v5`.
- `2026-02-19-suite-release-v2.csv` (`v2`)
  - Superseded by `v5` (did not include `PERF_META` and paired-ratio metadata
    in header).
- `2026-02-19-suite-release-v3.csv` (`v3`)
  - Superseded by `v5` (pre-async three-target matrix).

## Supplemental Hyperfine Runs

- `2026-02-19-hyperfine-idle.txt`
- `2026-02-19-hyperfine-echo-16k.txt`
- `2026-02-19-hyperfine-churn-64.txt`

## Supplemental Investigation Logs

- `2026-02-19-async-iteration4-notes.md`
  - architect/worker/reviewer async optimization loop summary
  - tool commands, findings, and `v4` -> `v5` delta notes
