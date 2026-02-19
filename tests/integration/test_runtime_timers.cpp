#include "simplenet/blocking/tcp.hpp"
#include "simplenet/nonblocking/tcp.hpp"
#include "simplenet/runtime/cancel.hpp"
#include "simplenet/runtime/event_loop.hpp"
#include "simplenet/runtime/io_ops.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace {

using namespace std::chrono_literals;

TEST(runtime_timers_test, async_sleep_completes_after_requested_duration) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    std::promise<simplenet::result<void>> sleep_promise;
    auto sleep_future = sleep_promise.get_future();
    std::atomic<long long> elapsed_ms{0};
    const auto started = std::chrono::steady_clock::now();

    auto sleeper = [&]() -> simplenet::runtime::task<void> {
        const auto sleep_result = co_await simplenet::runtime::async_sleep(60ms);
        elapsed_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count(),
                         std::memory_order_release);

        sleep_promise.set_value(sleep_result);
        loop.stop();
    };
    loop.spawn(sleeper());

    const auto run_result = loop.run();
    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();

    const auto sleep_result = sleep_future.get();
    ASSERT_TRUE(sleep_result.has_value()) << sleep_result.error().message();
    EXPECT_GE(elapsed_ms.load(std::memory_order_acquire), 40);
}

TEST(runtime_timers_test, async_sleep_observes_cancellation_token) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    simplenet::runtime::cancel_source cancel_source;
    std::promise<simplenet::result<void>> sleep_promise;
    auto sleep_future = sleep_promise.get_future();
    const auto started = std::chrono::steady_clock::now();

    auto sleeper = [&]() -> simplenet::runtime::task<void> {
        const auto sleep_result =
            co_await simplenet::runtime::async_sleep(2s, cancel_source.token());
        sleep_promise.set_value(sleep_result);
        loop.stop();
    };
    loop.spawn(sleeper());

    std::thread canceller([&]() {
        std::this_thread::sleep_for(50ms);
        cancel_source.request_stop();
    });

    const auto run_result = loop.run();
    canceller.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    const auto sleep_result = sleep_future.get();
    ASSERT_FALSE(sleep_result.has_value());
    EXPECT_EQ(sleep_result.error().value(), ECANCELED);

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    EXPECT_LT(elapsed, 1000);
}

TEST(runtime_timers_test, read_with_timeout_returns_timed_out_error) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    auto listener_result = simplenet::nonblocking::tcp_listener::bind(
        simplenet::nonblocking::endpoint::loopback(0), 16);
    ASSERT_TRUE(listener_result.has_value())
        << listener_result.error().message();
    auto listener = std::move(listener_result.value());

    auto port_result = listener.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    std::promise<simplenet::result<std::size_t>> read_promise;
    auto read_future = read_promise.get_future();

    auto server = [&]() -> simplenet::runtime::task<void> {
        auto accept_result = co_await simplenet::runtime::async_accept(listener);
        if (!accept_result.has_value()) {
            read_promise.set_value(
                simplenet::err<std::size_t>(accept_result.error()));
            loop.stop();
            co_return;
        }

        auto peer = std::move(accept_result.value());
        std::array<std::byte, 32> buffer{};
        auto read_result = co_await simplenet::runtime::async_read_some_with_timeout(
            peer, std::span<std::byte>{buffer}, 80ms);
        read_promise.set_value(read_result);
        loop.stop();
    };
    loop.spawn(server());

    std::promise<simplenet::result<void>> client_promise;
    auto client_future = client_promise.get_future();
    std::thread client_thread([&, port = port_result.value()]() {
        auto client_result = simplenet::blocking::tcp_stream::connect(
            simplenet::blocking::endpoint::loopback(port));
        if (!client_result.has_value()) {
            client_promise.set_value(simplenet::err<void>(client_result.error()));
            return;
        }

        std::this_thread::sleep_for(250ms);
        client_promise.set_value(simplenet::ok());
    });

    const auto run_result = loop.run();
    client_thread.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    const auto client_result = client_future.get();
    ASSERT_TRUE(client_result.has_value()) << client_result.error().message();

    const auto read_result = read_future.get();
    ASSERT_FALSE(read_result.has_value());
    EXPECT_EQ(read_result.error().value(), ETIMEDOUT);
}

} // namespace
