#include "simplenet/blocking/udp.hpp"

#include <array>
#include <cerrno>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace {

TEST(blocking_udp_test, ping_pong_on_loopback) {
    auto server_result =
        simplenet::blocking::udp_socket::bind(simplenet::blocking::endpoint::loopback(0));
    ASSERT_TRUE(server_result.has_value()) << server_result.error().message();
    auto server = std::move(server_result.value());

    auto port_result = server.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    auto client_result =
        simplenet::blocking::udp_socket::bind(simplenet::blocking::endpoint::loopback(0));
    ASSERT_TRUE(client_result.has_value()) << client_result.error().message();
    auto client = std::move(client_result.value());

    std::promise<simplenet::result<void>> server_promise;
    std::future<simplenet::result<void>> server_future = server_promise.get_future();

    std::thread server_thread(
        [&server, promise = std::move(server_promise)]() mutable {
            std::array<std::byte, 64> inbound{};
            const auto receive_result =
                server.recv_from(std::span<std::byte>{inbound});
            if (!receive_result.has_value()) {
                promise.set_value(simplenet::err<void>(receive_result.error()));
                return;
            }

            const auto packet = receive_result.value();
            const auto send_result = server.send_to(
                std::span<const std::byte>{inbound}.first(packet.size),
                packet.from);
            if (!send_result.has_value()) {
                promise.set_value(simplenet::err<void>(send_result.error()));
                return;
            }
            promise.set_value(simplenet::ok());
        });

    const std::array<std::byte, 4> payload{
        static_cast<std::byte>('p'),
        static_cast<std::byte>('i'),
        static_cast<std::byte>('n'),
        static_cast<std::byte>('g'),
    };

    const auto send_result =
        client.send_to(std::span<const std::byte>{payload},
                       simplenet::blocking::endpoint::loopback(port_result.value()));
    ASSERT_TRUE(send_result.has_value()) << send_result.error().message();
    ASSERT_EQ(send_result.value(), payload.size());

    std::array<std::byte, 64> response{};
    const auto receive_result =
        client.recv_from(std::span<std::byte>{response});
    ASSERT_TRUE(receive_result.has_value()) << receive_result.error().message();
    ASSERT_EQ(receive_result.value().size, payload.size());

    const auto server_status = server_future.get();
    server_thread.join();
    ASSERT_TRUE(server_status.has_value()) << server_status.error().message();

    const std::array<std::byte, 4> echoed{
        response[0],
        response[1],
        response[2],
        response[3],
    };
    EXPECT_EQ(echoed, payload);
}

TEST(blocking_udp_test, recv_from_empty_buffer_returns_einval) {
    auto socket_result =
        simplenet::blocking::udp_socket::bind(simplenet::blocking::endpoint::loopback(0));
    ASSERT_TRUE(socket_result.has_value()) << socket_result.error().message();
    auto socket = std::move(socket_result.value());

    std::array<std::byte, 0> empty{};
    const auto receive_result = socket.recv_from(std::span<std::byte>{empty});
    ASSERT_FALSE(receive_result.has_value());
    EXPECT_EQ(receive_result.error().value(), EINVAL);
}

} // namespace
