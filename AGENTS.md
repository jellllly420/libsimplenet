# AGENTS.md

This file is for coding agents working in `libsimplenet`.
Treat it as the project-operating manual.

## 1. Project Intent

`libsimplenet` is a C++23 Linux networking library intended to evolve from experimental to production-grade.
It emphasizes:

- predictable performance
- clear architecture
- modern C++23 async style
- robust testing and reproducibility
- practical API usability

Current maturity: **experimental, performance-focused**.

## 2. Source of Truth (Where To Look First)

- Global product/engineering plan:
  - `planning/ROADMAP.md`
  - `planning/plans/`
- Public-facing summary:
  - `README.md`
- Internal implementation docs:
  - `docs/development/`
- User/API docs:
  - `docs/usage/`
- Core runtime code:
  - `include/simplenet/runtime/`
  - `src/runtime/`
- Benchmark harness and artifacts:
  - `scripts/run_perf_suite.sh`
  - `docs/development/perf-data/`

## 3. Non-Negotiable Engineering Rules

1. Preserve correctness first.
2. Benchmark claims must be reproducible from files/commands in repo.
3. No benchmark “wins” without fairness controls.
4. Keep public README clean; internal experiment labels/details belong in `docs/development/`.
5. Do not leak personal/sensitive machine/user details into tracked docs/artifacts.

## 4. Build, Test, Coverage, Perf

### Standard build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Release perf build

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DSIMPLENET_BUILD_BOOST_BENCHMARKS=ON
cmake --build build-perf -j
bash ./scripts/run_perf_suite.sh
```

### Coverage

```bash
cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DSIMPLENET_ENABLE_COVERAGE=ON
cmake --build build-coverage -j
cmake --build build-coverage --target coverage
```

## 5. Runtime Model (Important)

Current runtime (`io_context`/`engine`) is **single-thread oriented per context**.

- One `run()` loop drives one backend event loop instance.
- `stop()` is thread-safe/responsive from external thread.
- Do **not** assume multi-thread `run()` sharing semantics like Boost.Asio.

Implication:
- Multi-core server scaling is currently app-level (multi-process or multiple contexts/listeners), not built-in shared-executor multi-threading.

## 6. Known Gaps (Do Not Hide)

- No first-class multi-core runtime orchestration model.
- No TLS abstraction in runtime APIs.
- No complete async UDP API parity.
- No strand/executor-style serialization abstraction.

When documenting or benchmarking, state these gaps plainly.

## 7. Performance Engineering Workflow (Required)

Use a loop:

1. **Plan**: identify scenario + hypothesis.
2. **Implement**: make focused low-risk change.
3. **Review (clean context)**: independent reviewer/architect checks fairness + correctness.
4. **Re-measure**: run suite; persist artifacts.
5. **Decide**: keep/iterate/revert.

Repeat until reviewer says acceptable.

### Role split guideline

- Architect: diagnose root causes, verify fairness design, propose ranked fixes.
- Worker: implement narrowly scoped changes and validate tests.
- Reviewer: re-check with clean context, findings first, explicit verdict.

## 8. Benchmark Fairness Checklist

Any new comparison vs Boost.Asio (or others) must keep:

1. Same build tier (`Release`) and machine/container.
2. Same scenario parameters (iterations/payload/concurrency).
3. Same transport model and client driver shape.
4. Alternating order for repeated runs.
5. Even repeat count for full order balance.
6. Paired-ratio reporting (`PERF_PAIRED_MEDIAN`) as primary signal.
7. Explicit skip metadata if a backend is unavailable.

If one of these is missing, comparison is incomplete.

## 9. Historical Mistakes and Fixes (Learned)

### Mistake: comparing async server implementations with non-equivalent clients
- Fix: both async benchmarks now use neutral POSIX blocking clients.

### Mistake: run-order bias in single-pass benchmarks
- Fix: repeated alternating pairwise execution with even repeats and paired medians.

### Mistake: not benchmarking core async path enough
- Fix: explicit async 3-target suite:
  - `libsimplenet(epoll)`
  - `libsimplenet(io_uring)`
  - `boost_asio(epoll)`

### Mistake: putting internal experiment labels in public README
- Fix: keep public README high-level; internal iteration/version detail in development docs.

## 10. Documentation Rules

### `README.md` (public-facing)
- Keep stable and user-oriented.
- Include capability snapshot, high-level performance summary, and usage entry points.
- Avoid internal iteration labels (`vX`, “iteration N”) and raw debug narrative.

### `docs/development/` (internal-facing)
- Store deep implementation details, investigation logs, benchmarking methodology, and iteration notes.
- Include command lines, artifact file paths, and interpretation notes.

### `docs/usage/` (API consumer-facing)
- Keep practical and concise.
- Show build/run examples and common operational guidance.

## 11. Sensitive Data / Privacy Hygiene

Before finishing any change:

1. Search for accidental personal leakage:
   - user names
   - home paths (`/Users/...`, `/home/...`)
   - machine hostnames if user-identifying
   - local workspace absolute paths
2. Redact benchmark metadata if it contains user-identifying details.
3. Keep data useful while sanitizing identity-bearing fields.

Recommended scan:

```bash
rg -n "(/Users/|/home/|host=|git_sha=worktree-|docker-desktop)"
```

## 12. Code Style and Safety

- C++ standard: C++23.
- Respect `.clang-format` and `.clang-tidy`.
- Keep changes minimal and scoped.
- Prefer explicit error handling and `result<T>` semantics already used in codebase.
- Do not introduce blocking operations inside async runtime paths.
- Validate with tests after behavior/perf changes.

## 13. Performance Change Acceptance Criteria

A performance patch should include:

1. Hypothesis (why this should help).
2. Exact touched files/functions.
3. Before/after numbers from persisted artifacts.
4. Correctness test results.
5. Residual risk notes (e.g., workload-specific wins).

Reject patch if:
- fairness is unclear
- gains are noise-only
- correctness confidence regresses

## 14. Recommended Next Technical Priorities

1. Multi-core runtime strategy (worker threads / multi-reactor design).
2. Continue async-path micro-optimizations with measured ROI.
3. TLS and async UDP API expansion.
4. More stress/soak tests and failure-injection scenarios.
5. Optional privacy-aware mode in perf suite metadata emission.

## 15. Agent Done Checklist

Before completing a task:

1. Build passes.
2. Relevant tests pass.
3. Docs updated in correct layer (README vs development vs usage).
4. Perf artifacts/logs persisted when performance is discussed.
5. Sensitive data scan completed and cleaned.
6. Explain remaining gaps honestly.
