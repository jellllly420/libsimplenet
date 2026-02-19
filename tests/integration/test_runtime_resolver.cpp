#include "simplenet/runtime/cancel.hpp"
#include "simplenet/runtime/event_loop.hpp"
#include "simplenet/runtime/resolver.hpp"

#include <algorithm>
#include <cerrno>
#include <future>
#include <gtest/gtest.h>

namespace {

TEST(runtime_resolver_test, parse_and_format_ipv4_endpoint_round_trip) {
    const auto parsed = simplenet::runtime::parse_ipv4_endpoint("127.0.0.1:8080");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message();
    EXPECT_EQ(parsed.value().host, "127.0.0.1");
    EXPECT_EQ(parsed.value().port, 8080);
    EXPECT_EQ(simplenet::runtime::format_endpoint(parsed.value()), "127.0.0.1:8080");
}

TEST(runtime_resolver_test, parse_ipv4_endpoint_rejects_invalid_input) {
    EXPECT_FALSE(simplenet::runtime::parse_ipv4_endpoint("127.0.0.1").has_value());
    EXPECT_FALSE(simplenet::runtime::parse_ipv4_endpoint("bad-ip:80").has_value());
    EXPECT_FALSE(
        simplenet::runtime::parse_ipv4_endpoint("127.0.0.1:70000").has_value());
}

TEST(runtime_resolver_test, async_resolve_returns_localhost_endpoints) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    std::promise<simplenet::result<std::vector<simplenet::runtime::endpoint>>>
        resolve_promise;
    auto resolve_future = resolve_promise.get_future();

    auto resolver_task = [&]() -> simplenet::runtime::task<void> {
        auto resolve_result =
            co_await simplenet::runtime::async_resolve("localhost", "80");
        resolve_promise.set_value(std::move(resolve_result));
        loop.stop();
    };
    loop.spawn(resolver_task());

    const auto run_result = loop.run();
    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();

    const auto resolve_result = resolve_future.get();
    ASSERT_TRUE(resolve_result.has_value()) << resolve_result.error().message();
    ASSERT_FALSE(resolve_result.value().empty());
    EXPECT_TRUE(std::all_of(
        resolve_result.value().begin(), resolve_result.value().end(),
        [](const simplenet::runtime::endpoint& value) { return value.port == 80; }));
}

TEST(runtime_resolver_test, async_resolve_observes_cancellation_before_start) {
    simplenet::runtime::event_loop loop;
    ASSERT_TRUE(loop.valid());

    simplenet::runtime::cancel_source source;
    source.request_stop();

    std::promise<simplenet::result<std::vector<simplenet::runtime::endpoint>>>
        resolve_promise;
    auto resolve_future = resolve_promise.get_future();

    auto resolver_task = [&]() -> simplenet::runtime::task<void> {
        auto resolve_result = co_await simplenet::runtime::async_resolve(
            "localhost", "80", source.token());
        resolve_promise.set_value(std::move(resolve_result));
        loop.stop();
    };
    loop.spawn(resolver_task());

    const auto run_result = loop.run();
    ASSERT_TRUE(run_result.has_value()) << run_result.error().message();

    const auto resolve_result = resolve_future.get();
    ASSERT_FALSE(resolve_result.has_value());
    EXPECT_EQ(resolve_result.error().value(), ECANCELED);
}

} // namespace
