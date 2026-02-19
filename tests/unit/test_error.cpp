#include "simplenet/core/error.hpp"

#include <cerrno>
#include <gtest/gtest.h>

namespace {

TEST(error_test, maps_common_errno_values) {
    const auto retry = simplenet::make_error_from_errno(EAGAIN);
    const auto reset = simplenet::make_error_from_errno(ECONNRESET);
    const auto timeout = simplenet::make_error_from_errno(ETIMEDOUT);

    EXPECT_EQ(retry.value(), EAGAIN);
    EXPECT_EQ(reset.value(), ECONNRESET);
    EXPECT_EQ(timeout.value(), ETIMEDOUT);
}

TEST(error_test, uses_current_errno_by_default) {
    errno = ETIMEDOUT;
    const auto value = simplenet::error::from_errno();

    EXPECT_EQ(value.value(), ETIMEDOUT);
    EXPECT_FALSE(value.message().empty());
}

} // namespace
