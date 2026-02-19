# WS1: Foundation and Build Plan

## Deliverables

- CMake project with options for tests, examples, and coverage.
- Public headers in `include/simplenet/`.
- Sources in `src/`, tests in `tests/`, examples in `example/`.
- Imported `.clang-format` and `.clang-tidy` from `../cpp20-netlib`.

## Tasks

1. Create root `CMakeLists.txt` with modular subdirectories.
2. Add interface/compiled library targets and strict compile options.
3. Add third-party setup for GoogleTest via `FetchContent`.
4. Add helper CMake module for coverage instrumentation.

## Acceptance Criteria

- `cmake -S . -B build` configures cleanly.
- Style/lint config files present and consumed by tooling.
