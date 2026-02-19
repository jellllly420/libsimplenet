# Testing and Coverage

## Test Strategy

- Unit tests validate core primitives (`error`, `result`, `unique_fd`).
- Integration tests validate runtime behavior:
  - epoll readiness and coroutine scheduling
  - io_uring readiness and async TCP flows
  - timer/cancellation correctness
  - resolver behavior
  - backpressure queue behavior
  - backend selection in `runtime::engine`

## Commands

Prerequisites:

- CMake 3.24+
- Ninja (or another CMake generator)
- C++23 compiler
- `liburing` development package
- `gcovr` (coverage mode only)
- network access to download GoogleTest on first configure

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Coverage build:

```bash
cmake -S . -B build-coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIMPLENET_ENABLE_COVERAGE=ON
cmake --build build-coverage -j
cmake --build build-coverage --target coverage
```

Coverage target gates currently enforced:

- line coverage >= `75%`
- function coverage >= `85%`
- branch coverage >= `50%`

Artifacts:

- XML: `build-coverage/coverage/coverage.xml`
- HTML: `build-coverage/coverage/index.html`
- Latest measured summary: `docs/development/coverage-report.md`
