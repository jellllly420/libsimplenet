# Backend Design

## epoll Backend

- Uses edge-triggered readiness (`EPOLLET`) with explicit read/write masks.
- Wait registrations track optional deadlines and timeout errors.
- Reactor wait timeout adapts to nearest coroutine deadline.

## io_uring Backend

- Uses poll-add/poll-remove submission for readiness parity with epoll operations.
- Completion queue entries are mapped back to waiter registrations through `user_data` keys.
- Queue depth is configurable through `runtime::engine` / `io_context` constructor.

## Behavioral Contract Across Backends

- Same coroutine APIs (`async_accept`, `async_read_some`, `async_sleep`, etc.).
- Timeout semantics return `ETIMEDOUT`.
- Cancellation semantics return `ECANCELED`.
