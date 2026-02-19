# Examples

Built binaries:

- `simplenet_echo_server`
- `simplenet_echo_client`
- `simplenet_backend_switch`
- `simplenet_perf_reactor_wait`
- `simplenet_perf_tcp_echo_libsimplenet`
- `simplenet_perf_connection_churn_libsimplenet`
- `simplenet_perf_async_echo_libsimplenet`

## Echo Demo

1. Start server:

```bash
./build/example/simplenet_echo_server 8080
```

2. Run client:

```bash
./build/example/simplenet_echo_client 127.0.0.1 8080 "hello"
```

Argument notes:

- Server/client ports must be in `[1, 65535]`; invalid values exit with code `2`.
- `echo_client` defaults to `127.0.0.1:8080` and payload `hello libsimplenet`.
- Extra positional arguments are rejected with usage output (exit code `2`).

## Backend Probe

```bash
./build/example/simplenet_backend_switch epoll
./build/example/simplenet_backend_switch io_uring
```

Argument notes:

- Accepted backend values are exactly `epoll` or `io_uring`.
- Any other value exits with code `2`.
- Extra positional arguments are rejected with usage output (exit code `2`).

## Reactor Microbenchmark

```bash
./build/example/simplenet_perf_reactor_wait 250000
```

This benchmark prints CSV rows for:
- allocation-heavy epoll wait baseline
- current reused-scratch-buffer path
- computed speedup factor

## Boost.Asio Microbenchmark

Source file:
- `example/perf_boost_asio_wait.cpp`
- `example/perf_tcp_echo_boost_asio.cpp`
- `example/perf_connection_churn_boost_asio.cpp`
- `example/perf_async_echo_boost_asio.cpp`

Build/run examples (same CMake toolchain as libsimplenet):

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DSIMPLENET_BUILD_BOOST_BENCHMARKS=ON
cmake --build build-perf --target \
  simplenet_perf_async_echo_libsimplenet \
  simplenet_perf_boost_asio_wait \
  simplenet_perf_tcp_echo_boost_asio \
  simplenet_perf_connection_churn_boost_asio \
  simplenet_perf_async_echo_boost_asio
./build-perf/example/simplenet_perf_boost_asio_wait 250000
```

## Full Comparison Suite

```bash
bash ./scripts/run_perf_suite.sh
```

This emits parseable lines for:

- idle wait latency
- TCP echo at multiple payload sizes
- async TCP echo with neutral POSIX blocking clients:
  - `libsimplenet` backend `epoll`
  - `libsimplenet` backend `io_uring` (when available)
  - `boost_asio` backend `epoll`
- connection churn at multiple concurrency levels

Output rows:

- `PERF_RUN,...` for each repeated run (`PERF_REPEATS`, default `6`, must be even)
- `PERF_MEDIAN,...` for the median headline number per implementation/scenario
- `PERF_PAIRED_MEDIAN,...` for median paired ratios (preferred fairness signal)
- `PERF_META_ASYNC,...` with async suite parameters and `io_uring` availability
- `PERF_SKIP,...` when async `io_uring` is not available on the host

Latest persisted suite baseline:

- `docs/development/perf-data/2026-02-19-suite-release-v5.csv`
