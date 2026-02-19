#include "simplenet/core/unique_fd.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <unistd.h>

namespace {

std::array<int, 2> make_pipe() {
    std::array<int, 2> fds{};
    if (::pipe(fds.data()) != 0) {
        throw std::runtime_error("pipe creation failed");
    }
    return fds;
}

void expect_fd_is_closed(int fd) {
    errno = 0;
    EXPECT_EQ(::fcntl(fd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(unique_fd_test, default_constructed_is_invalid) {
    const simplenet::unique_fd fd;
    EXPECT_FALSE(fd.valid());
    EXPECT_EQ(fd.get(), -1);
}

TEST(unique_fd_test, move_constructor_transfers_ownership_once) {
    const auto fds = make_pipe();

    simplenet::unique_fd read_end{fds[0]};
    simplenet::unique_fd write_end{fds[1]};
    EXPECT_TRUE(write_end.valid());

    const int transferred_fd = read_end.get();
    simplenet::unique_fd moved{std::move(read_end)};

    EXPECT_FALSE(read_end.valid());
    EXPECT_EQ(read_end.get(), -1);
    EXPECT_EQ(moved.get(), transferred_fd);
    EXPECT_NE(::fcntl(moved.get(), F_GETFD), -1);
}

TEST(unique_fd_test, move_assignment_closes_previous_descriptor) {
    const auto first = make_pipe();
    const auto second = make_pipe();

    simplenet::unique_fd target{first[0]};
    simplenet::unique_fd first_write_end{first[1]};
    EXPECT_TRUE(first_write_end.valid());

    simplenet::unique_fd source{second[0]};
    simplenet::unique_fd second_write_end{second[1]};
    EXPECT_TRUE(second_write_end.valid());

    const int old_target_fd = target.get();
    const int source_fd = source.get();

    target = std::move(source);

    expect_fd_is_closed(old_target_fd);
    EXPECT_FALSE(source.valid());
    EXPECT_EQ(target.get(), source_fd);
}

TEST(unique_fd_test, release_transfers_ownership_without_closing) {
    const auto fds = make_pipe();

    simplenet::unique_fd read_end{fds[0]};
    simplenet::unique_fd write_end{fds[1]};
    EXPECT_TRUE(write_end.valid());

    const int released = read_end.release();
    EXPECT_FALSE(read_end.valid());
    EXPECT_NE(::fcntl(released, F_GETFD), -1);

    const auto close_result = simplenet::close_fd(released);
    EXPECT_TRUE(close_result.has_value());
}

TEST(unique_fd_test, reset_closes_old_descriptor_and_adopts_new_one) {
    const auto first = make_pipe();
    const auto second = make_pipe();

    simplenet::unique_fd read_end{first[0]};
    simplenet::unique_fd first_write_end{first[1]};
    EXPECT_TRUE(first_write_end.valid());

    simplenet::unique_fd second_write_end{second[1]};
    EXPECT_TRUE(second_write_end.valid());

    const int old_fd = read_end.get();
    read_end.reset(second[0]);

    expect_fd_is_closed(old_fd);
    EXPECT_EQ(read_end.get(), second[0]);
    EXPECT_NE(::fcntl(read_end.get(), F_GETFD), -1);
}

TEST(unique_fd_test, destructor_closes_valid_descriptor) {
    int fd_to_check = -1;
    {
        const auto fds = make_pipe();
        simplenet::unique_fd read_end{fds[0]};
        simplenet::unique_fd write_end{fds[1]};
        EXPECT_TRUE(write_end.valid());
        fd_to_check = read_end.get();
    }

    expect_fd_is_closed(fd_to_check);
}

TEST(unique_fd_test, close_fd_reports_error_for_invalid_descriptor) {
    const auto close_result = simplenet::close_fd(-1);
    ASSERT_FALSE(close_result.has_value());
    EXPECT_EQ(close_result.error().value(), EBADF);
}

} // namespace
