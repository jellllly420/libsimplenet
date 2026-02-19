# Backend Selection

Use `runtime::engine` or `io_context` and select backend explicitly.

```cpp
simplenet::io_context epoll_ctx{simplenet::runtime::engine::backend::epoll};
simplenet::io_context uring_ctx{simplenet::runtime::engine::backend::io_uring, 512};
```

Recommendations:

- Start with `epoll` for broad kernel compatibility.
- Use `io_uring` when kernel/runtime environment supports it and workload benefits from lower syscall overhead.
- Always check `valid()` before calling `run()`.
