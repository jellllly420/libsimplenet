# Async Performance Iteration 4 Notes

## Goal

Reduce libsimplenet async echo overhead versus Boost.Asio while keeping
comparison fairness and correctness.

## Inputs

- Baseline artifact: `2026-02-19-suite-release-v4.csv`
- Three comparison targets:
  - `libsimplenet` backend `epoll`
  - `libsimplenet` backend `io_uring`
  - `boost_asio` backend `epoll`

## Fairness Checklist

- Same container, kernel, and machine path for all targets.
- Same build tier (`Release`, `-O3` equivalent).
- Same scenario parameters (`iterations`, payload sizes, `connections`).
- Same async client driver for both implementations (neutral POSIX blocking sockets).
- Pairwise alternating run order with even repeats.
- Paired median ratio (`PERF_PAIRED_MEDIAN`) used as primary comparison signal.

## Tooling Used

- `perf stat -r 5 -d -d -d`
- `valgrind --tool=callgrind`
- `callgrind_annotate`
- full-suite runner `scripts/run_perf_suite.sh`

## Commands (Representative)

```bash
# perf stat (payload 1024)
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

# callgrind (payload 1024)
valgrind --tool=callgrind \
  --callgrind-out-file=docs/development/perf-data/2026-02-19-callgrind-async-libsimplenet-epoll.out \
  ./build-perf/example/simplenet_perf_async_echo_libsimplenet \
  --backend epoll --iterations 800 --payload-size 1024 --connections 8

valgrind --tool=callgrind \
  --callgrind-out-file=docs/development/perf-data/2026-02-19-callgrind-async-boost-epoll.out \
  ./build-perf/example/simplenet_perf_async_echo_boost_asio \
  --iterations 800 --payload-size 1024 --connections 8
```

## Findings

- perf-stat elapsed-time means (payload `1024`):
  - libsimplenet/epoll: `~0.0869s`
  - libsimplenet/io_uring: `~0.0823s`
  - boost_asio/epoll: `~0.0782s`
- callgrind traces on libsimplenet showed repeated async exact-IO coroutine/wait
  transitions as hot paths.
- Architect hypothesis: nested `co_await async_read_some(...)` /
  `co_await async_write_some(...)` inside exact-IO loops creates avoidable
  task-frame churn.

## Change Applied

File changed:

- `src/runtime/io_ops.cpp`

Optimization:

- Rewrote `async_read_exact` and `async_write_all` to use direct
  `stream.read_some`/`stream.write_some` retry loops.
- On would-block, directly `co_await wait_readable` / `wait_writable`.
- Preserved behavior:
  - `ECONNRESET` on zero-byte read before exact completion.
  - `EPIPE` on zero-byte write before full flush.
  - same non-would-block error propagation.

## Results (v4 -> v5)

- New baseline artifact: `2026-02-19-suite-release-v5.csv`

libsimplenet async median deltas:

- payload `64`:
  - epoll: `85.219 -> 81.295` ms (`-4.61%`)
  - io_uring: `82.978 -> 78.613` ms (`-5.26%`)
- payload `1024`:
  - epoll: `86.071 -> 81.737` ms (`-5.04%`)
  - io_uring: `83.344 -> 80.023` ms (`-3.98%`)
- payload `16384`:
  - epoll: `115.954 -> 116.015` ms (`+0.05%`)
  - io_uring: `106.499 -> 107.356` ms (`+0.81%`)

Paired ratios (v5, `boost_over_libs`, closer to `1.0` means smaller gap):

- payload `64`: epoll `0.921939`, io_uring `0.961543`
- payload `1024`: epoll `0.917978`, io_uring `0.951200`
- payload `16384`: epoll `0.907431`, io_uring `0.998898`

## Validation

```bash
ctest --test-dir build --output-on-failure
```

- Result: `46/46` tests passed.
