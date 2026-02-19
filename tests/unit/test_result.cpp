#include "simplenet/core/result.hpp"

#include <cerrno>
#include <gtest/gtest.h>

namespace {

simplenet::result<int> parse_positive(int value) {
    if (value > 0) {
        return value;
    }
    return simplenet::err<int>(simplenet::make_error_from_errno(EINVAL));
}

TEST(result_test, stores_success_value) {
    const auto value = parse_positive(7);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 7);
}

TEST(result_test, stores_failure_value) {
    const auto value = parse_positive(0);
    ASSERT_FALSE(value.has_value());
    EXPECT_EQ(value.error().value(), EINVAL);
}

TEST(result_test, supports_void_success_result) {
    const auto value = simplenet::ok();
    EXPECT_TRUE(value.has_value());
}

} // namespace
