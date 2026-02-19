# WS4: Test and Coverage Plan

## Deliverables

- Unit tests for core/result/task/cancel primitives.
- Integration tests for TCP accept/connect/read/write and timeout behavior.
- Coverage report generation scripts/targets with artifacts.

## Tasks

1. Add deterministic local loopback integration tests.
2. Add edge-case tests for partial writes and cancellation races.
3. Enable coverage flags for dedicated build preset.
4. Produce machine-readable and HTML coverage outputs.

## Acceptance Criteria

- `ctest` passes all tests.
- Coverage report is generated and references key modules.
