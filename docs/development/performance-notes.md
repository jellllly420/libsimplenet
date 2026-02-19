# Performance Notes

## Current Tuning

- Nonblocking sockets are created with `SOCK_NONBLOCK | SOCK_CLOEXEC` when available.
- Write path supports queue-based batching/backpressure via `runtime::queued_writer`.
- Read/write operations avoid unnecessary allocations and operate on `std::span` buffers.
- `runtime::queued_writer` supports both copy-in and move-in enqueue paths to
  reduce buffer copies in producer-heavy workloads.
- `epoll::reactor::wait` reuses a thread-local kernel event scratch buffer instead
  of allocating per wait call.
- Runtime loops (`event_loop` and `uring_event_loop`) avoid periodic wakeup polling:
  no-timeout waiters now block indefinitely until I/O or explicit wakeup.
- Timeout bookkeeping is cached; waiter timeout scans are skipped entirely when there
  are no timed waiters and only recomputed when timeout state changes.

## Planned Extensions

- Multi-shot accept/read experiments for `io_uring`.
- Batched completion dispatch and lock-free ready queue options.
- Optional zero-copy send support where kernel features allow it.

## Benchmark Focus Areas

- Latency percentiles under mixed read/write workloads.
- Throughput comparison of epoll vs io_uring by payload size.
- Backpressure behavior under slow consumer scenarios.
- Cross-library comparison vs Boost.Asio using `scripts/run_perf_suite.sh`
  with persisted outputs under `docs/development/perf-data/`.
