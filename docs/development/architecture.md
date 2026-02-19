# Architecture

## Goals

- Preserve runtime correctness under high concurrency.
- Keep API backend-agnostic while allowing backend-specific performance tuning.
- Favor RAII and explicit ownership boundaries.

## Layering

1. `core/`
   - `error`, `result`, `unique_fd`
2. `blocking/`
   - synchronous TCP/UDP baseline and endpoint utilities
3. `epoll/` and `uring/`
   - backend reactors
4. `runtime/`
   - coroutine task model, scheduler/event loops, async operations, resolver, backpressure
5. Public façade
   - `simplenet/simplenet.hpp`, `io_context`, `ip_tcp`

## Runtime Model

- `runtime::task<T>` is coroutine-native and scheduler-aware.
- Event loops own readiness state and waiter lifecycle.
- Async I/O operations (`runtime/io_ops`) are backend-independent and depend on scheduler hooks.
- `runtime::engine` selects either `event_loop` (`epoll`) or `uring_event_loop`.

## Concurrency and Safety

- Main event loop threads are single-threaded by design for predictable scheduling.
- Shared resolver state uses mutex-protected handoff from worker thread to coroutine poll loop.
- Resource ownership is explicit with move-only socket/file descriptor wrappers.
