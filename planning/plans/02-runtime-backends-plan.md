# WS2: Runtime and Backends Plan

## Deliverables

- Coroutine task abstraction and scheduler hooks.
- `epoll` event loop backend.
- `io_uring` event loop backend.
- Backend selector `engine`.

## Tasks

1. Define task/promise contract and scheduler interface.
2. Implement epoll reactor registration, readiness dispatch, timer handling.
3. Implement io_uring ring setup, CQ polling, operation completion mapping.
4. Implement backend selection and unified run/stop/spawn API.

## Acceptance Criteria

- Same async API works over both backends.
- Cancellation and timeout behavior consistent between backends.
