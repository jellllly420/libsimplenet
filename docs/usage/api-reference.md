# API Reference

For the full, symbol-level reference, build Doxygen docs:

```bash
cmake --build build --target doc
```

Then open `build/docs/doxygen/html/index.html`.

## Core

- `simplenet::error`
- `simplenet::result<T>` (`std::expected`-based)
- `simplenet::unique_fd`

## Blocking APIs

- `simplenet::blocking::tcp_listener`
- `simplenet::blocking::tcp_stream`
- `simplenet::blocking::udp_socket`
- helpers: `write_all`, `read_exact`

## Nonblocking + Runtime APIs

- `simplenet::nonblocking::tcp_listener`
- `simplenet::nonblocking::tcp_stream`
- `simplenet::runtime::task<T>`
- `simplenet::runtime::event_loop`
- `simplenet::runtime::uring_event_loop`
- `simplenet::runtime::engine`
- operations:
  - `async_accept`
  - `async_connect`
  - `async_read_some`
  - `async_write_some`
  - `async_read_exact`
  - `async_write_all`
  - `async_sleep`
  - timeout variants for read/write

## Flow-Control Helpers

- `simplenet::runtime::queued_writer`
  - `enqueue(std::span<const std::byte>)` (copy-in)
  - `enqueue(std::vector<std::byte>&&)` (owning/move-in path, avoids extra copy)

## Convenience Facade

- `simplenet::io_context`
- `simplenet::ip::tcp` aliases (`endpoint`, `socket`, `acceptor`)
