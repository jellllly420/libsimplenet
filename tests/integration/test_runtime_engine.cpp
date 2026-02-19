#include "simplenet/runtime/engine.hpp"
#include "simplenet/runtime/io_ops.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <unistd.h>

namespace {

TEST(runtime_engine_test, default_backend_is_epoll) {
    simplenet::runtime::engine runtime;
    EXPECT_EQ(runtime.selected_backend(), simplenet::runtime::engine::backend::epoll);
    EXPECT_TRUE(runtime.valid());
}

TEST(runtime_engine_test, epoll_backend_runs_simple_task) {
    simplenet::runtime::engine runtime{simplenet::runtime::engine::backend::epoll};
    ASSERT_TRUE(runtime.valid());

    std::promise<simplenet::result<void>> completion_promise;
    auto completion_future = completion_promise.get_future();

    auto work = [&]() -> simplenet::runtime::task<void> {
        completion_promise.set_value(simplenet::ok());
        runtime.stop();
        co_return;
    };
    runtime.spawn(work());

    const auto run_result = runtime.run();
    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();

    const auto completion_result = completion_future.get();
    ASSERT_TRUE(completion_result.has_value())
        << completion_result.error().message();
}

TEST(runtime_engine_test, uring_backend_runs_readiness_task_when_available) {
    simplenet::runtime::engine runtime{simplenet::runtime::engine::backend::io_uring};
    if (!runtime.valid()) {
        GTEST_SKIP() << "io_uring backend unavailable";
    }

    std::array<int, 2> pipe_fds{};
    ASSERT_EQ(::pipe2(pipe_fds.data(), O_NONBLOCK | O_CLOEXEC), 0);
    simplenet::unique_fd read_end{pipe_fds[0]};
    simplenet::unique_fd write_end{pipe_fds[1]};

    std::atomic<int> stage{0};
    std::promise<simplenet::result<void>> completion_promise;
    auto completion_future = completion_promise.get_future();

    auto work = [&]() -> simplenet::runtime::task<void> {
        stage.store(1, std::memory_order_release);

        const auto wait_result =
            co_await simplenet::runtime::wait_readable(read_end.get());
        if (!wait_result.has_value()) {
            completion_promise.set_value(simplenet::err<void>(wait_result.error()));
            runtime.stop();
            co_return;
        }

        if (stage.load(std::memory_order_acquire) != 2) {
            completion_promise.set_value(
                simplenet::err<void>(simplenet::make_error_from_errno(EINVAL)));
            runtime.stop();
            co_return;
        }

        completion_promise.set_value(simplenet::ok());
        runtime.stop();
    };
    runtime.spawn(work());

    std::thread writer([&]() {
        while (stage.load(std::memory_order_acquire) < 1) {
            std::this_thread::yield();
        }

        stage.store(2, std::memory_order_release);
        constexpr std::array<std::byte, 1> marker{std::byte{0x42}};
        (void)::write(write_end.get(), marker.data(), marker.size());
    });

    const auto run_result = runtime.run();
    writer.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    const auto completion_result = completion_future.get();
    ASSERT_TRUE(completion_result.has_value())
        << completion_result.error().message();
}

TEST(runtime_engine_test, epoll_backend_stop_from_external_thread_is_responsive) {
    using namespace std::chrono_literals;

    simplenet::runtime::engine runtime{simplenet::runtime::engine::backend::epoll};
    ASSERT_TRUE(runtime.valid());

    runtime.spawn([]() -> simplenet::runtime::task<void> {
        (void)co_await simplenet::runtime::async_sleep(5s);
        co_return;
    }());

    std::thread stopper([&]() {
        std::this_thread::sleep_for(50ms);
        runtime.stop();
    });

    const auto start = std::chrono::steady_clock::now();
    const auto run_result = runtime.run();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    stopper.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    EXPECT_LT(elapsed, 500ms);
}

TEST(runtime_engine_test, uring_backend_stop_from_external_thread_is_responsive) {
    using namespace std::chrono_literals;

    simplenet::runtime::engine runtime{simplenet::runtime::engine::backend::io_uring};
    if (!runtime.valid()) {
        GTEST_SKIP() << "io_uring backend unavailable";
    }

    runtime.spawn([]() -> simplenet::runtime::task<void> {
        (void)co_await simplenet::runtime::async_sleep(5s);
        co_return;
    }());

    std::thread stopper([&]() {
        std::this_thread::sleep_for(50ms);
        runtime.stop();
    });

    const auto start = std::chrono::steady_clock::now();
    const auto run_result = runtime.run();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    stopper.join();

    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();
    EXPECT_LT(elapsed, 1500ms);
}

} // namespace
