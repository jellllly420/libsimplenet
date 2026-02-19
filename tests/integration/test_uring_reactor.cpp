#include "simplenet/core/unique_fd.hpp"
#include "simplenet/uring/reactor.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <unistd.h>

namespace {

TEST(uring_reactor_test, poll_add_waits_for_pipe_readability) {
    auto reactor_result = simplenet::uring::reactor::create();
    if (!reactor_result.has_value()) {
        GTEST_SKIP() << "io_uring unavailable: "
                     << reactor_result.error().message();
    }
    auto reactor = std::move(reactor_result.value());

    std::array<int, 2> pipe_fds{};
    ASSERT_EQ(::pipe2(pipe_fds.data(), O_NONBLOCK | O_CLOEXEC), 0);
    simplenet::unique_fd read_end{pipe_fds[0]};
    simplenet::unique_fd write_end{pipe_fds[1]};

    constexpr std::uint64_t request_token = 1;
    const auto add_result =
        reactor.submit_poll_add(request_token, read_end.get(), POLLIN);
    ASSERT_TRUE(add_result.has_value()) << add_result.error().message();

    const auto submit_result = reactor.submit();
    ASSERT_TRUE(submit_result.has_value()) << submit_result.error().message();

    constexpr std::array<std::byte, 1> payload{std::byte{0x21}};
    ASSERT_EQ(::write(write_end.get(), payload.data(), payload.size()), 1);

    std::array<simplenet::uring::completion, 8> completions{};
    const auto wait_result =
        reactor.wait(completions, std::chrono::milliseconds{250});
    ASSERT_TRUE(wait_result.has_value()) << wait_result.error().message();
    ASSERT_GE(wait_result.value(), 1U);

    bool saw_pollin = false;
    for (std::size_t i = 0; i < wait_result.value(); ++i) {
        if (completions[i].user_data == request_token &&
            (completions[i].result & POLLIN) != 0) {
            saw_pollin = true;
            break;
        }
    }

    EXPECT_TRUE(saw_pollin);
}

TEST(uring_reactor_test, timeout_returns_zero_ready_completions) {
    auto reactor_result = simplenet::uring::reactor::create();
    if (!reactor_result.has_value()) {
        GTEST_SKIP() << "io_uring unavailable: "
                     << reactor_result.error().message();
    }
    auto reactor = std::move(reactor_result.value());

    std::array<int, 2> pipe_fds{};
    ASSERT_EQ(::pipe2(pipe_fds.data(), O_NONBLOCK | O_CLOEXEC), 0);
    simplenet::unique_fd read_end{pipe_fds[0]};
    simplenet::unique_fd write_end{pipe_fds[1]};

    constexpr std::uint64_t request_token = 2;
    const auto add_result =
        reactor.submit_poll_add(request_token, read_end.get(), POLLIN);
    ASSERT_TRUE(add_result.has_value()) << add_result.error().message();

    const auto submit_result = reactor.submit();
    ASSERT_TRUE(submit_result.has_value()) << submit_result.error().message();

    std::array<simplenet::uring::completion, 8> completions{};
    const auto wait_result =
        reactor.wait(completions, std::chrono::milliseconds{30});
    ASSERT_TRUE(wait_result.has_value()) << wait_result.error().message();
    EXPECT_EQ(wait_result.value(), 0U);

    const auto remove_result = reactor.submit_poll_remove(request_token);
    ASSERT_TRUE(remove_result.has_value()) << remove_result.error().message();
    ASSERT_TRUE(reactor.submit().has_value());
}

} // namespace
