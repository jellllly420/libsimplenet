#include "simplenet/blocking/tcp.hpp"
#include "simplenet/nonblocking/tcp.hpp"
#include "simplenet/runtime/event_loop.hpp"
#include "simplenet/runtime/io_ops.hpp"
#include "simplenet/runtime/write_queue.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

TEST(runtime_backpressure_test,
     queued_writer_enforces_watermarks_and_graceful_shutdown) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    auto listener_result = simplenet::nonblocking::tcp_listener::bind(
        simplenet::nonblocking::endpoint::loopback(0), 16);
    ASSERT_TRUE(listener_result.has_value())
        << listener_result.error().message();
    auto listener = std::move(listener_result.value());

    auto port_result = listener.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    std::vector<std::byte> chunk_a(6000, std::byte{0xA1});
    std::vector<std::byte> chunk_b(6000, std::byte{0xB2});
    std::vector<std::byte> chunk_c(64, std::byte{0xC3});
    const auto chunk_b_size = chunk_b.size();

    std::promise<simplenet::result<std::size_t>> server_promise;
    auto server_future = server_promise.get_future();
    auto server = [&]() -> simplenet::runtime::task<void> {
        auto accept_result = co_await simplenet::runtime::async_accept(listener);
        if (!accept_result.has_value()) {
            server_promise.set_value(
                simplenet::err<std::size_t>(accept_result.error()));
            loop.stop();
            co_return;
        }

        simplenet::runtime::queued_writer writer(
            std::move(accept_result.value()),
            simplenet::runtime::watermarks{4096, 8192});

        const auto enqueue_a =
            writer.enqueue(std::span<const std::byte>{chunk_a});
        if (!enqueue_a.has_value() ||
            enqueue_a.value() != simplenet::runtime::backpressure_state::normal) {
            server_promise.set_value(
                simplenet::err<std::size_t>(simplenet::make_error_from_errno(EPROTO)));
            loop.stop();
            co_return;
        }

        const auto enqueue_b =
            writer.enqueue(std::move(chunk_b));
        if (!enqueue_b.has_value() ||
            enqueue_b.value() !=
                simplenet::runtime::backpressure_state::high_watermark) {
            server_promise.set_value(
                simplenet::err<std::size_t>(simplenet::make_error_from_errno(EPROTO)));
            loop.stop();
            co_return;
        }

        const auto enqueue_reject =
            writer.enqueue(std::span<const std::byte>{chunk_c});
        if (enqueue_reject.has_value() ||
            enqueue_reject.error().value() != EWOULDBLOCK) {
            server_promise.set_value(
                simplenet::err<std::size_t>(simplenet::make_error_from_errno(EPROTO)));
            loop.stop();
            co_return;
        }

        const auto flush_result = co_await writer.flush(2s);
        if (!flush_result.has_value()) {
            server_promise.set_value(
                simplenet::err<std::size_t>(flush_result.error()));
            loop.stop();
            co_return;
        }

        if (writer.queued_bytes() != 0 || writer.high_watermark_active()) {
            server_promise.set_value(
                simplenet::err<std::size_t>(simplenet::make_error_from_errno(EPROTO)));
            loop.stop();
            co_return;
        }

        const auto enqueue_c =
            writer.enqueue(std::span<const std::byte>{chunk_c});
        if (!enqueue_c.has_value()) {
            server_promise.set_value(simplenet::err<std::size_t>(enqueue_c.error()));
            loop.stop();
            co_return;
        }

        const auto shutdown_result = co_await writer.graceful_shutdown(2s);
        if (!shutdown_result.has_value()) {
            server_promise.set_value(
                simplenet::err<std::size_t>(shutdown_result.error()));
            loop.stop();
            co_return;
        }

        server_promise.set_value(chunk_a.size() + chunk_b_size +
                                 chunk_c.size());
        loop.stop();
    };
    loop.spawn(server());

    std::promise<simplenet::result<std::size_t>> client_promise;
    auto client_future = client_promise.get_future();
    std::thread client_thread([&, port = port_result.value()]() {
        auto client_result = simplenet::blocking::tcp_stream::connect(
            simplenet::blocking::endpoint::loopback(port));
        if (!client_result.has_value()) {
            client_promise.set_value(
                simplenet::err<std::size_t>(client_result.error()));
            return;
        }
        auto client = std::move(client_result.value());

        std::size_t received = 0;
        std::array<std::byte, 4096> buffer{};
        while (true) {
            auto read_result = client.read_some(std::span<std::byte>{buffer});
            if (!read_result.has_value()) {
                client_promise.set_value(
                    simplenet::err<std::size_t>(read_result.error()));
                return;
            }
            if (read_result.value() == 0U) {
                break;
            }
            received += read_result.value();
        }

        client_promise.set_value(received);
    });

    const auto run_result = loop.run();
    client_thread.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    const auto server_result = server_future.get();
    const auto client_result = client_future.get();
    ASSERT_TRUE(server_result.has_value()) << server_result.error().message();
    ASSERT_TRUE(client_result.has_value()) << client_result.error().message();
    EXPECT_EQ(client_result.value(), server_result.value());
}

} // namespace
