# Performance Engineering Lab Log

## Objective

Build a repeatable, multi-scenario performance comparison between `libsimplenet`
and Boost.Asio, then iterate on `libsimplenet` performance while preserving
correctness.

## Environment

- Date: 2026-02-19
- Runtime: devcontainer (`/workspaces/libsimplenet`)
- OS/base: Debian trixie container
- Compiler/toolchain: C++23 via project CMake toolchain

## Tooling Installed via Apt

Commands executed:

```bash
sudo apt-get update
sudo apt-get install -y libboost-dev libboost-system-dev hyperfine valgrind linux-perf
```

Installed packages (key):

- `libboost-dev`, `libboost-system-dev`
- `hyperfine`
- `valgrind`
- `linux-perf`

## Iteration Tracker

### Iteration 0 (Completed)

- Step 0.1: Corrected methodology issue discovered in prior benchmark pass:
  `libsimplenet` and Boost.Asio must be compared in equivalent optimization
  settings (Release-level builds).
- Step 0.2: Started architect+worker clean-context loop for scenario matrix and
  optimization plan.
- Step 0.3: Added multi-scenario benchmark harness and suite script.
- Step 0.4: Installed Boost from apt package manager and switched comparison
  from downloaded tarball to system packages.

### Iteration 1 (Completed)

- Step 1.1: Fairness audit and methodology hardening.
- Step 1.2: Ran multi-scenario matrix in Release mode and persisted outputs.
- Step 1.3: Profiled hot scenario with callgrind and applied low-risk TCP fast
  path optimization.
- Step 1.4: Re-ran correctness suite (`ctest`) to guard regressions.

### Iteration 2 (Completed)

- Step 2.1: Ran clean-context fairness architect review on harness methodology.
- Step 2.2: Removed Boost-only client allocation bias in echo/churn benchmarks.
- Step 2.3: Upgraded suite to repeated alternating-order execution with median
  aggregation for every scenario.
- Step 2.4: Re-ran full matrix and persisted new `v3` output.

### Iteration 3 (Completed)

- Step 3.1: Addressed async fairness findings by switching both async benchmark
  clients to a neutral raw POSIX blocking driver.
- Step 3.2: Decoupled Boost async benchmark client sockets from
  `boost::asio::io_context`.
- Step 3.3: Extended suite parser/output to include async pairwise comparisons
  and async metadata rows.
- Step 3.4: Added async comparison artifact path for next persisted suite run.

### Iteration 4 (Completed)

- Step 4.1: Architect review identified async coroutine frame churn in
  `async_read_exact` / `async_write_all` as a likely small-payload gap source.
- Step 4.2: Worker rewrote those helpers to inline nonblocking retry loops and
  remove nested per-chunk task allocation/await overhead.
- Step 4.3: Rebuilt Release perf targets and re-ran full suite; persisted
  `v5` output.
- Step 4.4: Clean-context reviewer validated fairness/correctness for this
  iteration and returned verdict `OK`.

## Performance Suite Commands (Expected)

```bash
# Build libsimplenet perf targets (Release)
cmake -S /workspaces/libsimplenet -B /workspaces/libsimplenet/build-perf \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIMPLENET_BUILD_BOOST_BENCHMARKS=ON
cmake --build /workspaces/libsimplenet/build-perf \
  --target simplenet_perf_reactor_wait \
           simplenet_perf_async_echo_libsimplenet \
           simplenet_perf_tcp_echo_libsimplenet \
           simplenet_perf_connection_churn_libsimplenet \
           simplenet_perf_boost_asio_wait \
           simplenet_perf_async_echo_boost_asio \
           simplenet_perf_tcp_echo_boost_asio \
           simplenet_perf_connection_churn_boost_asio

# Run full suite (idle wait + tcp echo + async tcp echo + connection churn)
/workspaces/libsimplenet/scripts/run_perf_suite.sh

# Optional parameter overrides
IDLE_ITERATIONS=300000 \
ECHO_ITERATIONS=4000 ECHO_PAYLOAD_SIZES=1024 ECHO_CONNECTIONS=16 \
ASYNC_ECHO_ITERATIONS=4000 ASYNC_ECHO_PAYLOAD_SIZES=1024 ASYNC_ECHO_CONNECTIONS=16 \
ASYNC_URING_QUEUE_DEPTH=512 \
CHURN_ITERATIONS=3000 CHURN_CONNECTION_LEVELS=64 \
PERF_REPEATS=8 \
/workspaces/libsimplenet/scripts/run_perf_suite.sh
```

## Fairness Rules Enforced

1. Same environment: both implementations run inside the same devcontainer.
2. Same optimization tier: Release-style binaries (`-O3` / `CMAKE_BUILD_TYPE=Release`).
3. Same scenario shapes: identical payload sizes, iterations, and connection counts.
4. Same transport model for compared cases: synchronous loopback TCP client/server
   in both implementations for echo/churn scenarios.
5. Async echo client fairness: both implementations use the same neutral
   blocking POSIX socket driver.
6. Repeatability: every scenario is run repeatedly with alternating order and
   median aggregation (`PERF_REPEATS`, default `6`, must be even).
7. Fairness-preferred ratio signal is paired per-repeat ratio median
   (`PERF_PAIRED_MEDIAN`), not just per-implementation median.
8. Async ordering method: pairwise alternation is applied per comparison pair
   (`libsimplenet/epoll` vs `boost_asio/epoll`; `libsimplenet/io_uring` vs
   `boost_asio/epoll` when available).

## Multi-Scenario Matrix Output

Persisted file:

- `docs/development/perf-data/2026-02-19-suite-release-v4.csv` (async-integrated
  suite output path)
- `docs/development/perf-data/2026-02-19-suite-release-v5.csv` (current baseline
  after async optimization pass)
- Index/deprecation marker: `docs/development/perf-data/README.md`

Scenarios covered:

- `idle_wait` (reactor/poll microbenchmark)
- `tcp_echo` payload sizes: `64`, `1024`, `16384`
- `async_tcp_echo` pairwise comparisons for:
  - `libsimplenet backend=epoll` vs `boost_asio backend=epoll`
  - `libsimplenet backend=io_uring` vs `boost_asio backend=epoll`
    (when `io_uring` is available)
- `connection_churn` connection levels: `16`, `32`, `64`

Median snapshot (from persisted CSV):

- Core scenarios (`v5`):
  - `idle_wait`: libsimplenet `102.506 ns/op` vs Boost.Asio `121.499 ns/op`
    (paired ratio `1.146551`, libsimplenet faster).
  - `tcp_echo payload=64`: libsimplenet `132.826 ms` vs Boost.Asio `132.773 ms`
    (paired ratio `0.997921`, near parity).
  - `tcp_echo payload=1024`: libsimplenet `133.246 ms` vs Boost.Asio `133.461 ms`
    (paired ratio `1.005084`, near parity).
  - `tcp_echo payload=16384`: libsimplenet `141.403 ms` vs Boost.Asio `142.093 ms`
    (paired ratio `1.001817`, near parity).
  - `connection_churn 16`: libsimplenet `286.614 ms` vs Boost.Asio `319.496 ms`
    (paired ratio `1.105672`, libsimplenet faster).
  - `connection_churn 32`: libsimplenet `501.872 ms` vs Boost.Asio `569.778 ms`
    (paired ratio `1.138939`, libsimplenet faster).
  - `connection_churn 64`: libsimplenet `969.782 ms` vs Boost.Asio `1101.996 ms`
    (paired ratio `1.134861`, libsimplenet faster).

- Async three-target scenarios (`v5`, `iterations=2000`, `connections=8`):
  - `payload=64`: libs/epoll `81.295 ms`, libs/io_uring `78.613 ms`,
    boost/epoll `74.750 ms`.
    - paired `boost_over_libs(epoll)=0.921939`
    - paired `boost_over_libs(io_uring)=0.961543`
  - `payload=1024`: libs/epoll `81.737 ms`, libs/io_uring `80.023 ms`,
    boost/epoll `74.964 ms`.
    - paired `boost_over_libs(epoll)=0.917978`
    - paired `boost_over_libs(io_uring)=0.951200`
  - `payload=16384`: libs/epoll `116.015 ms`, libs/io_uring `107.356 ms`,
    boost/epoll `109.773 ms`.
    - paired `boost_over_libs(epoll)=0.907431`
    - paired `boost_over_libs(io_uring)=0.998898`

- Iteration 4 async delta (`v4` -> `v5`, libsimplenet medians):
  - `payload=64`: epoll `-3.924 ms` (`-4.61%`), io_uring `-4.365 ms` (`-5.26%`).
  - `payload=1024`: epoll `-4.335 ms` (`-5.04%`), io_uring `-3.321 ms` (`-3.98%`).
  - `payload=16384`: epoll `+0.061 ms` (`+0.05%`), io_uring `+0.857 ms` (`+0.81%`).

## Repeated-Run Validation (Hyperfine)

Persisted files:

- `docs/development/perf-data/2026-02-19-hyperfine-idle.txt`
- `docs/development/perf-data/2026-02-19-hyperfine-echo-16k.txt`
- `docs/development/perf-data/2026-02-19-hyperfine-churn-64.txt`

Key outcomes:

- Idle wait (`300k` ops): libsimplenet `1.36x` faster (`±0.32`).
- Echo `16KiB`: Boost.Asio `1.02x` faster (`±0.06`).
- Connection churn `64`: libsimplenet `1.14x` faster (`±0.02`).

Interpretation:

- Single runs can mislead; repeated-suite medians (`v5`) are the default signal.
- Alternating order removes fixed first/second-run bias between implementations.
- Connection churn and idle-wait scenarios favor libsimplenet.
- TCP echo remains near parity across payload sizes.
- Async gap is workload-dependent:
  - small payloads (`64`, `1024`) still favor Boost.Asio for both libs backends.
  - at `16384`, libsimplenet `io_uring` is near parity with Boost.Asio while
    libsimplenet `epoll` trails.

## Profiling Tools and Findings

Tool used:

- `perf stat` (container-supported counters + elapsed time)
- `valgrind --tool=callgrind`

Commands (representative):

```bash
# Async three-target perf-stat sampling
perf stat -r 5 -d -d -d -- \
  ./build-perf/example/simplenet_perf_async_echo_libsimplenet \
  --backend epoll --iterations 2000 --payload-size 1024 --connections 8

perf stat -r 5 -d -d -d -- \
  ./build-perf/example/simplenet_perf_async_echo_libsimplenet \
  --backend io_uring --uring-queue-depth 512 \
  --iterations 2000 --payload-size 1024 --connections 8

perf stat -r 5 -d -d -d -- \
  ./build-perf/example/simplenet_perf_async_echo_boost_asio \
  --iterations 2000 --payload-size 1024 --connections 8

# Async callgrind comparison
valgrind --tool=callgrind \
  --callgrind-out-file=docs/development/perf-data/2026-02-19-callgrind-async-libsimplenet-epoll.out \
  ./build-perf/example/simplenet_perf_async_echo_libsimplenet \
  --backend epoll --iterations 800 --payload-size 1024 --connections 8

valgrind --tool=callgrind \
  --callgrind-out-file=docs/development/perf-data/2026-02-19-callgrind-async-boost-epoll.out \
  ./build-perf/example/simplenet_perf_async_echo_boost_asio \
  --iterations 800 --payload-size 1024 --connections 8

# Prior blocking echo profiling reference
valgrind --tool=callgrind --callgrind-out-file=/tmp/cg_snet2.out \
  ./build-perf/example/simplenet_perf_tcp_echo_libsimplenet \
  --iterations 1500 --payload-size 16384 --connections 8

valgrind --tool=callgrind --callgrind-out-file=/tmp/cg_boost2.out \
  ./build-perf/example/simplenet_perf_tcp_echo_boost_asio \
  --iterations 1500 --payload-size 16384 --connections 8
```

Findings:

- Async perf-stat elapsed-time means (payload `1024`, `r=5`):
  - libsimplenet/epoll: `~0.0869s`
  - libsimplenet/io_uring: `~0.0823s`
  - boost_asio/epoll: `~0.0782s`
  - Perf artifacts:
    - `docs/development/perf-data/2026-02-19-perfstat-async-libsimplenet-epoll-1024.txt`
    - `docs/development/perf-data/2026-02-19-perfstat-async-libsimplenet-io_uring-1024.txt`
    - `docs/development/perf-data/2026-02-19-perfstat-async-boost-epoll-1024.txt`
- Async callgrind showed heavy runtime cost around repeated coroutine/wait
  transitions in libsimplenet async exact-IO path, consistent with architect
  hypothesis of nested task frame churn.
  - libs artifact:
    `docs/development/perf-data/2026-02-19-callgrind-async-libsimplenet-epoll.txt`
  - boost artifact:
    `docs/development/perf-data/2026-02-19-callgrind-async-boost-epoll.txt`
- Prior blocking profiling remains valid: syscall wrappers (`send`/`recv`)
  dominate for both libraries on large echo payloads.

## Improvements Applied

### A) Fairness/Benchmarking Improvements

- Added mode selection to idle benchmark:
  - `example/perf_reactor_wait.cpp`
  - new `--mode all|alloc|reuse` to avoid accidental mixed-mode timing.
- Extended suite into repeated scenario matrix:
  - `scripts/run_perf_suite.sh`
  - now runs multiple payload sizes and connection levels across repeated runs.
  - alternates run order every repeat (`libsimplenet` first on odd rounds,
    `boost_asio` first on even rounds).
  - enforces even repeat counts for fully balanced run order.
  - emits per-run `PERF_RUN` rows, aggregate `PERF_MEDIAN` rows, and paired
    ratio `PERF_PAIRED_MEDIAN` rows.
  - parser hardened to require expected `impl` + `scenario` when extracting
    output rows (prevents accidental mis-parse if extra `PERF` lines appear).
- Added and used apt-managed Boost benchmarks (no tarball dependency):
  - `example/perf_tcp_echo_boost_asio.cpp`
  - `example/perf_connection_churn_boost_asio.cpp`
  - `example/perf_boost_asio_wait.cpp`
- Removed Boost-only socket allocation bias in compared scenarios:
  - switched `std::vector<std::unique_ptr<tcp::socket>>` to
    `std::vector<tcp::socket>` in:
    - `example/perf_tcp_echo_boost_asio.cpp`
    - `example/perf_connection_churn_boost_asio.cpp`

### B) Library Runtime/IO Improvements

- Applied low-risk fast path in blocking full-buffer IO:
  - file: `src/blocking/tcp.cpp`
  - `write_all` and `read_exact` now issue direct `send/recv` loops with
    `EINTR` retry, reducing wrapper overhead in high-throughput loops.

Rationale:

- Profiling showed these helpers are instruction hotspots under echo workloads.
- Change preserves behavior while reducing per-iteration overhead.

### C) Async Runtime Improvement (Iteration 4)

- Reduced coroutine/task churn in exact async I/O helpers:
  - file: `src/runtime/io_ops.cpp`
  - functions:
    - `async_read_exact(...)`
    - `async_write_all(...)`
- Change details:
  - replaced nested `co_await async_read_some(...)` / `co_await async_write_some(...)`
    inside exact loops with inline `stream.read_some(...)` / `stream.write_some(...)`
    retry loops.
  - preserved would-block handling via direct `co_await wait_readable(...)` /
    `co_await wait_writable(...)`.
  - preserved zero-byte error semantics (`ECONNRESET` / `EPIPE`) and error
    propagation behavior.

Rationale:

- Architect + callgrind analysis indicated nested per-chunk async task frames
  were a likely small-payload overhead source.
- This patch keeps behavior and fairness intact while lowering coroutine
  scheduling/allocation overhead in hot async echo loops.

## Correctness Verification After Performance Changes

Command:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DSIMPLENET_BUILD_BOOST_BENCHMARKS=ON
cmake --build build-perf -j
bash ./scripts/run_perf_suite.sh > docs/development/perf-data/2026-02-19-suite-release-v5.csv
```

Result:

- `46/46` tests passed.
- Full Release suite re-ran successfully and persisted `v5` artifact.
- Clean-context reviewer verdict for Iteration 4: `OK`.
