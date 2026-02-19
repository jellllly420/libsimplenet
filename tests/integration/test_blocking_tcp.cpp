#include "simplenet/blocking/tcp.hpp"

#include <array>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace {

TEST(blocking_tcp_test, echo_round_trip_on_loopback) {
    auto listener_result =
        simplenet::blocking::tcp_listener::bind(simplenet::blocking::endpoint::loopback(0));
    ASSERT_TRUE(listener_result.has_value())
        << listener_result.error().message();

    auto listener = std::move(listener_result.value());
    auto port_result = listener.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    std::promise<simplenet::result<void>> server_promise;
    std::future<simplenet::result<void>> server_future = server_promise.get_future();

    std::thread server_thread([&listener,
                               promise = std::move(server_promise)]() mutable {
        auto accepted_result = listener.accept();
        if (!accepted_result.has_value()) {
            promise.set_value(simplenet::err<void>(accepted_result.error()));
            return;
        }

        auto stream = std::move(accepted_result.value());
        std::array<std::byte, 11> payload{};

        const auto read_status =
            simplenet::blocking::read_exact(stream, std::span<std::byte>{payload});
        if (!read_status.has_value()) {
            promise.set_value(simplenet::err<void>(read_status.error()));
            return;
        }

        const auto write_status = simplenet::blocking::write_all(
            stream, std::span<const std::byte>{payload});
        promise.set_value(write_status);
    });

    auto client_result = simplenet::blocking::tcp_stream::connect(
        simplenet::blocking::endpoint::loopback(port_result.value()));
    ASSERT_TRUE(client_result.has_value()) << client_result.error().message();
    auto client = std::move(client_result.value());

    const std::array<std::byte, 11> request{
        static_cast<std::byte>('h'), static_cast<std::byte>('e'),
        static_cast<std::byte>('l'), static_cast<std::byte>('l'),
        static_cast<std::byte>('o'), static_cast<std::byte>('-'),
        static_cast<std::byte>('w'), static_cast<std::byte>('o'),
        static_cast<std::byte>('r'), static_cast<std::byte>('l'),
        static_cast<std::byte>('d'),
    };

    const auto write_status =
        simplenet::blocking::write_all(client, std::span<const std::byte>{request});
    ASSERT_TRUE(write_status.has_value()) << write_status.error().message();

    std::array<std::byte, 11> response{};
    const auto read_status =
        simplenet::blocking::read_exact(client, std::span<std::byte>{response});
    ASSERT_TRUE(read_status.has_value()) << read_status.error().message();

    const auto server_status = server_future.get();
    server_thread.join();

    ASSERT_TRUE(server_status.has_value()) << server_status.error().message();
    EXPECT_EQ(response, request);
}

} // namespace
