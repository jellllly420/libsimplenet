# Doxygen Maintenance Notes

This project uses Doxygen comments in public headers under `include/simplenet/`.

## Goals

- Keep API behavior and contracts discoverable from headers.
- Keep generated reference docs in sync with code changes.
- Fail fast on under-documented new APIs during review.

## Authoring Rules

- Add a `@brief` summary for every public class/struct/function/type alias.
- Add `@param` and `@return` where behavior is non-obvious.
- Prefer documenting observable behavior and ownership/lifetime rules.
- Avoid repeating trivial information already encoded by the type system.
- Update docs in the same commit as API changes.

## Build Integration

- CMake option: `SIMPLENET_BUILD_DOCS` (default `ON`).
- CMake target: `doc` (available when Doxygen is installed).
- Config template: `docs/Doxyfile.in`.

Generated output is written to:

- `build/docs/doxygen/html/index.html` (or matching build directory).

## Suggested Review Checklist

- New/changed public symbols in `include/simplenet/**` have Doxygen comments.
- Examples in `example/` still compile with changed signatures.
- `cmake --build <build-dir> --target doc` succeeds locally/CI.
