# Generated API Docs (Doxygen)

`libsimplenet` exposes Doxygen-annotated headers under `include/simplenet/`.

## Build the docs

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target doc
```

## Open the docs

- Main page: `build/docs/doxygen/html/index.html`

## Coverage scope

- Core types: errors, `result<T>`, `unique_fd`
- Blocking/nonblocking socket APIs
- Runtime scheduler/task/event-loop APIs
- Async operations (`runtime/io_ops.hpp`)
- Resolver and write-queue helpers
