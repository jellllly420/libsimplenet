# WS3: API Usability Plan

## Deliverables

- Public API set for TCP stream/listener, resolver, timers, async operations.
- Buffer-oriented read/write operations (`some`, `all`, `exact`).
- Timeout and cancellation variants.

## Tasks

1. Build endpoint/address abstraction.
2. Add nonblocking TCP wrappers and socket option helpers.
3. Add resolver abstraction (`getaddrinfo` based).
4. Expose ergonomic coroutine APIs inspired by Asio operation names.

## Acceptance Criteria

- Example echo server/client code stays concise and readable.
- API names are stable and backend-agnostic.
