#include "simplenet/simplenet.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

simplenet::runtime::task<void>
run_backend_probe(simplenet::io_context& context) {
    const auto sleep_result =
        co_await simplenet::runtime::async_sleep(std::chrono::milliseconds{50});
    if (!sleep_result.has_value()) {
        context.stop();
        co_return;
    }

    context.stop();
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: simplenet_backend_switch [epoll|io_uring]\n";
        return 2;
    }

    const std::string backend_name = argc > 1 ? argv[1] : "epoll";

    simplenet::runtime::engine::backend backend{};
    if (backend_name == "epoll") {
        backend = simplenet::runtime::engine::backend::epoll;
    } else if (backend_name == "io_uring") {
        backend = simplenet::runtime::engine::backend::io_uring;
    } else {
        std::cerr << "unknown backend '" << backend_name
                  << "', expected 'epoll' or 'io_uring'\n";
        return 2;
    }

    simplenet::io_context context{backend, 512};
    if (!context.valid()) {
        std::cerr << "backend unavailable: " << backend_name << '\n';
        return 1;
    }

    context.spawn(run_backend_probe(context));

    const auto run_status = context.run();
    if (!run_status.has_value()) {
        std::cerr << "runtime error: " << run_status.error().message() << '\n';
        return 1;
    }

    std::cout << "backend " << backend_name << " executed successfully\n";
    return 0;
}
