#include "simplenet/blocking/tcp.hpp"
#include "simplenet/nonblocking/tcp.hpp"
#include "simplenet/runtime/event_loop.hpp"
#include "simplenet/runtime/io_ops.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

TEST(runtime_coroutines_test, wait_readable_suspends_and_resumes_in_order) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    std::array<int, 2> pipe_fds{};
    ASSERT_EQ(::pipe2(pipe_fds.data(), O_NONBLOCK | O_CLOEXEC), 0);
    simplenet::unique_fd read_end{pipe_fds[0]};
    simplenet::unique_fd write_end{pipe_fds[1]};

    std::atomic<int> stage{0};
    std::promise<simplenet::result<void>> coroutine_promise;
    auto coroutine_future = coroutine_promise.get_future();

    auto coroutine = [&]() -> simplenet::runtime::task<void> {
        stage.store(1, std::memory_order_release);

        const auto wait_result =
            co_await simplenet::runtime::wait_readable(read_end.get());
        if (!wait_result.has_value()) {
            coroutine_promise.set_value(simplenet::err<void>(wait_result.error()));
            loop.stop();
            co_return;
        }

        if (stage.load(std::memory_order_acquire) != 2) {
            coroutine_promise.set_value(
                simplenet::err<void>(simplenet::make_error_from_errno(EINVAL)));
            loop.stop();
            co_return;
        }

        stage.store(3, std::memory_order_release);
        coroutine_promise.set_value(simplenet::ok());
        loop.stop();
    };
    loop.spawn(coroutine());

    std::thread writer_thread([&]() {
        while (stage.load(std::memory_order_acquire) < 1) {
            std::this_thread::yield();
        }
        stage.store(2, std::memory_order_release);
        constexpr std::array<std::byte, 1> marker{std::byte{0x42}};
        (void)::write(write_end.get(), marker.data(), marker.size());
    });

    const auto run_result = loop.run();
    writer_thread.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    const auto coroutine_result = coroutine_future.get();
    ASSERT_TRUE(coroutine_result.has_value())
        << coroutine_result.error().message();
    EXPECT_EQ(stage.load(std::memory_order_acquire), 3);
}

TEST(runtime_coroutines_test, async_accept_read_write_echo) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    constexpr std::size_t payload_size = 64U * 1024U;
    std::vector<std::byte> outbound(payload_size);
    for (std::size_t i = 0; i < outbound.size(); ++i) {
        outbound[i] = static_cast<std::byte>((i * 13U) % 251U);
    }

    auto listener_result = simplenet::nonblocking::tcp_listener::bind(
        simplenet::nonblocking::endpoint::loopback(0), 32);
    ASSERT_TRUE(listener_result.has_value())
        << listener_result.error().message();
    auto listener = std::move(listener_result.value());

    auto port_result = listener.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    std::promise<simplenet::result<void>> server_promise;
    auto server_future = server_promise.get_future();

    auto server_coroutine = [&]() -> simplenet::runtime::task<void> {
        auto accept_result = co_await simplenet::runtime::async_accept(listener);
        if (!accept_result.has_value()) {
            server_promise.set_value(simplenet::err<void>(accept_result.error()));
            loop.stop();
            co_return;
        }

        auto peer = std::move(accept_result.value());
        std::vector<std::byte> inbound(payload_size);

        auto read_status = co_await simplenet::runtime::async_read_exact(
            peer, std::span<std::byte>{inbound.data(), inbound.size()});
        if (!read_status.has_value()) {
            server_promise.set_value(simplenet::err<void>(read_status.error()));
            loop.stop();
            co_return;
        }

        auto write_status = co_await simplenet::runtime::async_write_all(
            peer, std::span<const std::byte>{inbound.data(), inbound.size()});
        if (!write_status.has_value()) {
            server_promise.set_value(simplenet::err<void>(write_status.error()));
            loop.stop();
            co_return;
        }

        server_promise.set_value(simplenet::ok());
        loop.stop();
    };
    loop.spawn(server_coroutine());

    std::promise<simplenet::result<void>> client_promise;
    auto client_future = client_promise.get_future();
    std::thread client_thread([&, port = port_result.value()]() {
        auto client_result = simplenet::blocking::tcp_stream::connect(
            simplenet::blocking::endpoint::loopback(port));
        if (!client_result.has_value()) {
            client_promise.set_value(simplenet::err<void>(client_result.error()));
            return;
        }

        auto client = std::move(client_result.value());
        const auto write_status = simplenet::blocking::write_all(
            client, std::span<const std::byte>{outbound});
        if (!write_status.has_value()) {
            client_promise.set_value(simplenet::err<void>(write_status.error()));
            return;
        }

        std::vector<std::byte> echoed(outbound.size());
        const auto read_status = simplenet::blocking::read_exact(
            client, std::span<std::byte>{echoed.data(), echoed.size()});
        if (!read_status.has_value()) {
            client_promise.set_value(simplenet::err<void>(read_status.error()));
            return;
        }

        if (echoed != outbound) {
            client_promise.set_value(
                simplenet::err<void>(simplenet::make_error_from_errno(EBADMSG)));
            return;
        }

        client_promise.set_value(simplenet::ok());
    });

    const auto run_result = loop.run();
    client_thread.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    const auto server_result = server_future.get();
    const auto client_result = client_future.get();
    ASSERT_TRUE(server_result.has_value()) << server_result.error().message();
    ASSERT_TRUE(client_result.has_value()) << client_result.error().message();
}

TEST(runtime_coroutines_test,
     async_connect_path_completes_handshake_and_round_trip) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    auto listener_result =
        simplenet::blocking::tcp_listener::bind(simplenet::blocking::endpoint::loopback(0));
    ASSERT_TRUE(listener_result.has_value())
        << listener_result.error().message();
    auto listener = std::move(listener_result.value());

    auto port_result = listener.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    constexpr std::array<std::byte, 8> message{
        static_cast<std::byte>('l'), static_cast<std::byte>('a'),
        static_cast<std::byte>('b'), static_cast<std::byte>('-'),
        static_cast<std::byte>('0'), static_cast<std::byte>('5'),
        static_cast<std::byte>('-'), static_cast<std::byte>('x'),
    };

    std::promise<simplenet::result<void>> server_promise;
    auto server_future = server_promise.get_future();
    std::thread server_thread([&]() {
        auto peer_result = listener.accept();
        if (!peer_result.has_value()) {
            server_promise.set_value(simplenet::err<void>(peer_result.error()));
            return;
        }

        auto peer = std::move(peer_result.value());
        std::array<std::byte, message.size()> incoming{};
        const auto read_status =
            simplenet::blocking::read_exact(peer, std::span<std::byte>{incoming});
        if (!read_status.has_value()) {
            server_promise.set_value(simplenet::err<void>(read_status.error()));
            return;
        }
        const auto write_status = simplenet::blocking::write_all(
            peer, std::span<const std::byte>{incoming});
        if (!write_status.has_value()) {
            server_promise.set_value(simplenet::err<void>(write_status.error()));
            return;
        }
        server_promise.set_value(simplenet::ok());
    });

    std::promise<simplenet::result<void>> client_promise;
    auto client_future = client_promise.get_future();
    auto client_coroutine = [&]() -> simplenet::runtime::task<void> {
        auto connect_result = co_await simplenet::runtime::async_connect(
            simplenet::nonblocking::endpoint::loopback(port_result.value()));
        if (!connect_result.has_value()) {
            client_promise.set_value(simplenet::err<void>(connect_result.error()));
            loop.stop();
            co_return;
        }

        auto stream = std::move(connect_result.value());
        auto write_status = co_await simplenet::runtime::async_write_all(
            stream, std::span<const std::byte>{message});
        if (!write_status.has_value()) {
            client_promise.set_value(simplenet::err<void>(write_status.error()));
            loop.stop();
            co_return;
        }

        std::array<std::byte, message.size()> echoed{};
        auto read_status = co_await simplenet::runtime::async_read_exact(
            stream, std::span<std::byte>{echoed});
        if (!read_status.has_value()) {
            client_promise.set_value(simplenet::err<void>(read_status.error()));
            loop.stop();
            co_return;
        }

        if (echoed != message) {
            client_promise.set_value(
                simplenet::err<void>(simplenet::make_error_from_errno(EBADMSG)));
            loop.stop();
            co_return;
        }

        client_promise.set_value(simplenet::ok());
        loop.stop();
    };
    loop.spawn(client_coroutine());

    const auto run_result = loop.run();
    server_thread.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    const auto server_result = server_future.get();
    const auto client_result = client_future.get();
    ASSERT_TRUE(server_result.has_value()) << server_result.error().message();
    ASSERT_TRUE(client_result.has_value()) << client_result.error().message();
}

} // namespace
