# libsimplenet PRD Roadmap

## 1. Product Intent

`libsimplenet` is a production-oriented C++23 async networking library for Linux with first-class `epoll` and `io_uring` backends. It prioritizes predictable performance, robust API ergonomics, and maintainable industrial structure.

## 2. PRD Scope (Mapped from Request)

1. Performance-first runtime for real use, not educational scaffolding.
2. More complete API surface than foundational labs, inspired by Boost.Asio usage patterns.
3. Idiomatic C++23 design (coroutines, concepts where useful, `std::expected`-style result handling).
4. Industrial project structure and coding standards.
5. Reuse `../cpp20-netlib/.clang-format` and `../cpp20-netlib/.clang-tidy`.
6. Thorough documentation split into:
   - `docs/development/` for architecture and implementation details.
   - `docs/usage/` for API usage and recipes.
7. Thorough test suite with high coverage and tool-generated proof.
8. Several examples under `example/`.

## 3. Feature Baseline (Labs 1-10 Equivalent, Productionized)

- Foundation primitives: error, result, RAII file descriptors, endpoints, socket helpers.
- Async runtime:
  - coroutine task abstraction
  - scheduler/event loop contract
  - timer and cancellation support
  - backpressure-aware write queue
- Linux backends:
  - `epoll` backend
  - `io_uring` backend
  - runtime backend selection
- Async operations:
  - accept/connect
  - read/write some
  - read/write exact/all
  - timeout wrappers
- Resolver/endpoints utilities.

## 4. Production Extensions Beyond Educational Labs

- API organization similar to `io_context + socket + acceptor + resolver + steady_timer` style.
- Backend-agnostic high-level API boundary.
- Configuration knobs for queue depth, poll batch size, and timer granularity.
- Structured error propagation and categorized error codes.
- Validation tests around cancellation, partial writes, and shutdown races.
- Benchmarks for backend comparison and regression checks.

## 5. Non-Functional Requirements

- C++23, Linux-first.
- High signal static analysis defaults.
- Deterministic CI-style local build/test commands.
- Coverage report output persisted in `build/coverage/`.

## 6. Milestones

1. M0: Planning artifacts approved and persisted.
2. M1: Build/tooling skeleton, style config, project layout.
3. M2: Core types + coroutine runtime contract.
4. M3: `epoll` backend + async TCP ops.
5. M4: `io_uring` backend + backend selector.
6. M5: Tests + coverage automation + examples.
7. M6: Documentation hardening and review loop until clean.

## 7. Exit Criteria

- All planned tests pass in devcontainer.
- Coverage tool output generated and retained.
- Examples compile and run.
- Reviewer pass with no blocking issues.
- Docs cover both implementation internals and end-user usage.
