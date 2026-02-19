#include "simplenet/simplenet.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {

simplenet::runtime::task<void>
run_echo_server(simplenet::runtime::engine& engine,
                simplenet::nonblocking::tcp_listener listener) {
    while (true) {
        auto accepted = co_await simplenet::runtime::async_accept(listener);
        if (!accepted.has_value()) {
            std::cerr << "accept failed: " << accepted.error().message()
                      << '\n';
            engine.stop();
            co_return;
        }

        auto stream = std::move(accepted.value());
        std::array<std::byte, 4096> buffer{};

        while (true) {
            auto read_result =
                co_await simplenet::runtime::async_read_some(stream, buffer);
            if (!read_result.has_value()) {
                if (read_result.error().value() == EAGAIN ||
                    read_result.error().value() == EWOULDBLOCK) {
                    continue;
                }
                break;
            }

            const auto read_count = read_result.value();
            if (read_count == 0) {
                break;
            }

            auto write_result = co_await simplenet::runtime::async_write_all(
                stream, std::span<const std::byte>{buffer.data(), read_count});
            if (!write_result.has_value()) {
                break;
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: simplenet_echo_server [port]\n";
        return 2;
    }

    std::uint16_t port = 8080;
    if (argc > 1) {
        try {
            std::size_t consumed = 0;
            const auto text = std::string{argv[1]};
            const auto parsed = std::stoul(text, &consumed);
            if (consumed != text.size()) {
                std::cerr << "invalid port argument\n";
                return 2;
            }
            if (parsed == 0U || parsed > 65535U) {
                std::cerr << "port must be in range [1, 65535]\n";
                return 2;
            }
            port = static_cast<std::uint16_t>(parsed);
        } catch (const std::exception&) {
            std::cerr << "invalid port argument\n";
            return 2;
        }
    }

    auto listener_result = simplenet::nonblocking::tcp_listener::bind(
        simplenet::nonblocking::endpoint::loopback(port), 128);
    if (!listener_result.has_value()) {
        std::cerr << "bind failed: " << listener_result.error().message()
                  << '\n';
        return 1;
    }

    simplenet::runtime::engine engine;
    if (!engine.valid()) {
        std::cerr << "runtime engine init failed\n";
        return 1;
    }

    engine.spawn(run_echo_server(engine, std::move(listener_result.value())));
    const auto run_status = engine.run();
    if (!run_status.has_value()) {
        std::cerr << "runtime failed: " << run_status.error().message() << '\n';
        return 1;
    }
    return 0;
}
