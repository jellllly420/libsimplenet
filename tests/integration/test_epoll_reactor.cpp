#include "simplenet/blocking/tcp.hpp"
#include "simplenet/epoll/reactor.hpp"
#include "simplenet/nonblocking/tcp.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace {

simplenet::result<void> run_epoll_echo_server(simplenet::nonblocking::tcp_listener listener,
                                        std::size_t expected_bytes) {
    auto reactor_result = simplenet::epoll::reactor::create();
    if (!reactor_result.has_value()) {
        return simplenet::err<void>(reactor_result.error());
    }
    auto reactor = std::move(reactor_result.value());

    const auto add_listener_result =
        reactor.add(listener.native_handle(), EPOLLIN | EPOLLET);
    if (!add_listener_result.has_value()) {
        return add_listener_result;
    }

    simplenet::nonblocking::tcp_stream peer{};
    bool peer_registered = false;
    std::vector<std::byte> payload(expected_bytes);
    std::size_t read_total = 0;
    std::size_t write_total = 0;
    int idle_rounds = 0;
    std::array<simplenet::epoll::ready_event, 16> events{};

    while (write_total < expected_bytes) {
        const auto wait_result =
            reactor.wait(events, std::chrono::milliseconds{250});
        if (!wait_result.has_value()) {
            return simplenet::err<void>(wait_result.error());
        }

        if (wait_result.value() == 0U) {
            ++idle_rounds;
            if (idle_rounds > 20) {
                return simplenet::err<void>(simplenet::make_error_from_errno(ETIMEDOUT));
            }
            continue;
        }
        idle_rounds = 0;

        for (std::size_t i = 0; i < wait_result.value(); ++i) {
            const auto& event = events[i];
            if (event.fd == listener.native_handle()) {
                while (true) {
                    auto accepted_result = listener.accept();
                    if (!accepted_result.has_value()) {
                        if (simplenet::nonblocking::is_would_block(
                                accepted_result.error())) {
                            break;
                        }
                        return simplenet::err<void>(accepted_result.error());
                    }

                    peer = std::move(accepted_result.value());
                    peer_registered = true;
                    const auto sendbuf_result = peer.set_send_buffer_size(4096);
                    if (!sendbuf_result.has_value()) {
                        return sendbuf_result;
                    }

                    const auto add_peer =
                        reactor.add(peer.native_handle(),
                                    EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP |
                                        EPOLLERR | EPOLLHUP);
                    if (!add_peer.has_value()) {
                        return add_peer;
                    }
                }
                continue;
            }

            if (!peer_registered || event.fd != peer.native_handle()) {
                continue;
            }

            if (simplenet::epoll::has_event(event.events,
                                      EPOLLERR | EPOLLHUP | EPOLLRDHUP) &&
                write_total < expected_bytes) {
                if (read_total < expected_bytes) {
                    return simplenet::err<void>(
                        simplenet::make_error_from_errno(ECONNRESET));
                }
            }

            if (simplenet::epoll::has_event(event.events, EPOLLIN)) {
                while (read_total < expected_bytes) {
                    auto read_result = peer.read_some(
                        std::span<std::byte>{payload}.subspan(read_total));
                    if (!read_result.has_value()) {
                        if (simplenet::nonblocking::is_would_block(
                                read_result.error())) {
                            break;
                        }
                        return simplenet::err<void>(read_result.error());
                    }

                    if (read_result.value() == 0U) {
                        return simplenet::err<void>(
                            simplenet::make_error_from_errno(ECONNRESET));
                    }
                    read_total += read_result.value();
                }
            }

            if (simplenet::epoll::has_event(event.events, EPOLLOUT)) {
                while (write_total < read_total) {
                    auto write_result = peer.write_some(
                        std::span<const std::byte>{payload.data() + write_total,
                                                   read_total - write_total});
                    if (!write_result.has_value()) {
                        if (simplenet::nonblocking::is_would_block(
                                write_result.error())) {
                            break;
                        }
                        return simplenet::err<void>(write_result.error());
                    }

                    if (write_result.value() == 0U) {
                        return simplenet::err<void>(
                            simplenet::make_error_from_errno(EPIPE));
                    }
                    write_total += write_result.value();
                }
            }
        }
    }

    return simplenet::ok();
}

simplenet::result<std::size_t>
run_churn_server(simplenet::nonblocking::tcp_listener listener,
                 std::size_t expected_connections) {
    auto reactor_result = simplenet::epoll::reactor::create();
    if (!reactor_result.has_value()) {
        return simplenet::err<std::size_t>(reactor_result.error());
    }
    auto reactor = std::move(reactor_result.value());

    const auto add_listener =
        reactor.add(listener.native_handle(), EPOLLIN | EPOLLET);
    if (!add_listener.has_value()) {
        return simplenet::err<std::size_t>(add_listener.error());
    }

    std::size_t accepted_count = 0;
    int idle_rounds = 0;
    std::array<simplenet::epoll::ready_event, 16> events{};

    while (accepted_count < expected_connections) {
        const auto wait_result =
            reactor.wait(events, std::chrono::milliseconds{250});
        if (!wait_result.has_value()) {
            return simplenet::err<std::size_t>(wait_result.error());
        }

        if (wait_result.value() == 0U) {
            ++idle_rounds;
            if (idle_rounds > 30) {
                return simplenet::err<std::size_t>(
                    simplenet::make_error_from_errno(ETIMEDOUT));
            }
            continue;
        }
        idle_rounds = 0;

        for (std::size_t i = 0; i < wait_result.value(); ++i) {
            const auto& event = events[i];
            if (event.fd != listener.native_handle()) {
                continue;
            }

            while (true) {
                auto accepted_result = listener.accept();
                if (!accepted_result.has_value()) {
                    if (simplenet::nonblocking::is_would_block(
                            accepted_result.error())) {
                        break;
                    }
                    return simplenet::err<std::size_t>(accepted_result.error());
                }
                ++accepted_count;
            }
        }
    }

    return accepted_count;
}

TEST(epoll_reactor_test, lifecycle_add_wait_remove_on_pipe) {
    std::array<int, 2> pipe_fds{};
    ASSERT_EQ(::pipe2(pipe_fds.data(), O_NONBLOCK | O_CLOEXEC), 0);
    simplenet::unique_fd read_end{pipe_fds[0]};
    simplenet::unique_fd write_end{pipe_fds[1]};

    auto reactor_result = simplenet::epoll::reactor::create();
    ASSERT_TRUE(reactor_result.has_value()) << reactor_result.error().message();
    auto reactor = std::move(reactor_result.value());

    const auto add_result = reactor.add(read_end.get(), EPOLLIN | EPOLLET);
    ASSERT_TRUE(add_result.has_value()) << add_result.error().message();

    constexpr std::array<std::byte, 1> one_byte{std::byte{0x7F}};
    ASSERT_EQ(::write(write_end.get(), one_byte.data(), one_byte.size()), 1);

    std::array<simplenet::epoll::ready_event, 8> events{};
    const auto wait_result =
        reactor.wait(events, std::chrono::milliseconds{200});
    ASSERT_TRUE(wait_result.has_value()) << wait_result.error().message();
    ASSERT_GE(wait_result.value(), 1U);

    bool saw_read_end = false;
    for (std::size_t i = 0; i < wait_result.value(); ++i) {
        if (events[i].fd == read_end.get() &&
            simplenet::epoll::has_event(events[i].events, EPOLLIN)) {
            saw_read_end = true;
            break;
        }
    }
    EXPECT_TRUE(saw_read_end);

    const auto remove_result = reactor.remove(read_end.get());
    ASSERT_TRUE(remove_result.has_value()) << remove_result.error().message();
}

TEST(epoll_reactor_test, edge_triggered_echo_handles_partial_io_and_eagain) {
    constexpr std::size_t payload_size = 512U * 1024U;

    auto listener_result = simplenet::nonblocking::tcp_listener::bind(
        simplenet::nonblocking::endpoint::loopback(0), 32);
    ASSERT_TRUE(listener_result.has_value())
        << listener_result.error().message();
    auto listener = std::move(listener_result.value());

    auto port_result = listener.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    auto server_future = std::async(
        std::launch::async, [listener = std::move(listener)]() mutable {
            return run_epoll_echo_server(std::move(listener), payload_size);
        });

    auto client_result = simplenet::blocking::tcp_stream::connect(
        simplenet::blocking::endpoint::loopback(port_result.value()));
    ASSERT_TRUE(client_result.has_value()) << client_result.error().message();
    auto client = std::move(client_result.value());

    std::vector<std::byte> outbound(payload_size);
    for (std::size_t i = 0; i < outbound.size(); ++i) {
        outbound[i] = static_cast<std::byte>(i % 251U);
    }
    std::vector<std::byte> inbound(payload_size);

    const auto write_status =
        simplenet::blocking::write_all(client, std::span<const std::byte>{outbound});
    ASSERT_TRUE(write_status.has_value()) << write_status.error().message();

    const auto read_status =
        simplenet::blocking::read_exact(client, std::span<std::byte>{inbound});
    ASSERT_TRUE(read_status.has_value()) << read_status.error().message();

    const auto server_status = server_future.get();
    ASSERT_TRUE(server_status.has_value()) << server_status.error().message();
    EXPECT_EQ(inbound, outbound);
}

TEST(epoll_reactor_test, connection_churn_accepts_many_short_lived_clients) {
    constexpr std::size_t connection_count = 300;

    auto listener_result = simplenet::nonblocking::tcp_listener::bind(
        simplenet::nonblocking::endpoint::loopback(0), 64);
    ASSERT_TRUE(listener_result.has_value())
        << listener_result.error().message();
    auto listener = std::move(listener_result.value());

    auto port_result = listener.local_port();
    ASSERT_TRUE(port_result.has_value()) << port_result.error().message();

    auto server_future = std::async(
        std::launch::async, [listener = std::move(listener)]() mutable {
            return run_churn_server(std::move(listener), connection_count);
        });

    for (std::size_t i = 0; i < connection_count; ++i) {
        auto client_result = simplenet::blocking::tcp_stream::connect(
            simplenet::blocking::endpoint::loopback(port_result.value()));
        ASSERT_TRUE(client_result.has_value())
            << client_result.error().message();
    }

    const auto accepted_result = server_future.get();
    ASSERT_TRUE(accepted_result.has_value())
        << accepted_result.error().message();
    EXPECT_EQ(accepted_result.value(), connection_count);
}

} // namespace
