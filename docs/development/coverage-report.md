# Coverage Evidence

## Command

```bash
cmake -S . -B build-coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIMPLENET_ENABLE_COVERAGE=ON -DSIMPLENET_BUILD_TESTS=ON -DSIMPLENET_BUILD_EXAMPLES=OFF
cmake --build build-coverage -j
cmake --build build-coverage --target coverage
```

## Latest Result

From the most recent `coverage` target run:

- Date (UTC): `2026-02-19`
- Build profile: `Debug`, `SIMPLENET_ENABLE_COVERAGE=ON`, `SIMPLENET_BUILD_EXAMPLES=OFF`
- Environment: Dev Container `mcr.microsoft.com/devcontainers/cpp:latest`
- Lines: `75.1%` (`1048 / 1396`)
- Functions: `90.1%` (`154 / 171`)
- Branches: `52.5%` (`545 / 1038`)

## Artifacts

- XML: `build-coverage/coverage/coverage.xml`
- HTML: `build-coverage/coverage/index.html`

## Notes

- Coverage currently focuses on `src/` implementation files.
- Integration tests heavily exercise runtime, backend selection, timers, resolver, and backpressure behavior.
- Coverage command enforces minimum thresholds:
  - lines >= `75%`
  - functions >= `85%`
  - branches >= `50%`
