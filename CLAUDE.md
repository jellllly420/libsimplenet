# CLAUDE.md — libsimplenet

Project operating manual for Claude Code.
Full engineering details are in `AGENTS.md`; this file distils the rules that matter most during every session.

## CRITICAL: Toolchain

**The host machine has no C++ toolchain.** Every build, test, and script invocation must go through the devcontainer CLI:

```bash
devcontainer exec --workspace-folder /Volumes/Dev/libsimplenet <cmd>
```

Never run `cmake`, `ctest`, `g++`, `clang++`, or `python3` directly on the host.

## Standard Commands

```bash
# Debug build (ASAN + UBSAN on by default)
devcontainer exec --workspace-folder /Volumes/Dev/libsimplenet \
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
devcontainer exec --workspace-folder /Volumes/Dev/libsimplenet \
  cmake --build build -j
devcontainer exec --workspace-folder /Volumes/Dev/libsimplenet \
  ctest --test-dir build --output-on-failure   # must show 46/46

# Release perf build + suite
devcontainer exec --workspace-folder /Volumes/Dev/libsimplenet \
  bash ./scripts/run_perf_suite.sh \
  | tee docs/development/perf-data/$(date +%Y-%m-%d)-suite-release-vN.csv

# Regenerate README figures after perf update
devcontainer exec --workspace-folder /Volumes/Dev/libsimplenet \
  python3 scripts/generate_perf_figures.py
```

## Non-Negotiable Rules

1. **Correctness first.** All 46 tests must pass before any commit.
2. **No benchmark claims without fairness controls** — alternating order, even repeats, `PERF_PAIRED_MEDIAN` as primary signal.
3. **Sensitive data scan before finishing any change:**
   ```bash
   rg -n "(/Users/|/home/|host=|git_sha=worktree-|docker-desktop)"
   ```
   Redact `PERF_META` CSV fields: set `git_sha`, `generated_utc`, `cxx`, `kernel`, `host` → `redacted` (match the pattern in `v5`/`v6` files).
4. **Keep README clean.** No internal iteration labels (`vN`, "Iteration N") in `README.md`; those belong in `docs/development/`.

## Documentation Layers

| Path | Audience | What goes there |
|---|---|---|
| `README.md` | Public | Capability table, high-level perf summary, usage entry points |
| `docs/development/` | Internal | Implementation details, profiling logs, iteration notes, raw commands |
| `docs/usage/` | API consumers | Build/run examples, API reference |
| `docs/development/performance-lab-log.md` | Internal | Per-iteration tracker and median snapshots |
| `docs/development/perf-data/*.csv` | Artifacts | Raw suite output (PERF_META must be redacted) |

## Perf Suite Workflow

1. Run `run_perf_suite.sh` and tee output to a new dated CSV in `docs/development/perf-data/`.
2. Redact `PERF_META` line in the CSV.
3. Update hardcoded arrays in `scripts/generate_perf_figures.py` and regenerate figures.
4. Update Core Scenario Matrix and Async I/O Matrix tables in `README.md` (date the heading).
5. Add iteration entry and median snapshot to `docs/development/performance-lab-log.md`.

## Code Conventions

- C++23; `result<T>` / `error` semantics throughout (`simplenet::make_error_from_errno`).
- Async functions are coroutines: `task<result<T>> foo(...) { co_return ...; }`
- Single-threaded per context — do not add blocking calls inside async runtime paths.
- Prefer explicit error handling; no silent fallbacks.
- Respect `.clang-format` and `.clang-tidy`; keep changes minimal and scoped.

## Known Gaps (State Honestly)

- No multi-core runtime orchestration.
- No TLS abstraction.
- No async UDP.
- No strand/executor model.

## Agent Done Checklist

Before completing any task:

1. Build passes (`cmake --build`).
2. 46/46 tests pass (`ctest`).
3. Docs updated in the correct layer (README vs development vs usage).
4. Perf artifacts persisted when performance is discussed.
5. Sensitive data scan done and PERF_META redacted.
6. Remaining gaps stated honestly.
