# Quickstart

## Include

```cpp
#include "simplenet/simplenet.hpp"
```

## Create Runtime

```cpp
simplenet::io_context ctx{simplenet::runtime::engine::backend::epoll};
if (!ctx.valid()) {
    return;
}
```

## Spawn Coroutine Task

```cpp
ctx.spawn([]() -> simplenet::runtime::task<void> {
    const auto sleep = co_await simplenet::runtime::async_sleep(std::chrono::milliseconds{10});
    (void)sleep;
    co_return;
}());
ctx.run();
```
