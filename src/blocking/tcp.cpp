#include "simplenet/blocking/tcp.hpp"

#include "socket_helpers.hpp"

#include <cerrno>
#include <sys/socket.h>

namespace simplenet::blocking {

tcp_stream::tcp_stream(simplenet::unique_fd fd) noexcept : fd_(std::move(fd)) {}

result<tcp_stream> tcp_stream::connect(const endpoint& remote) noexcept {
    const auto maybe_addr = detail::to_sockaddr(remote);
    if (!maybe_addr.has_value()) {
        return err<tcp_stream>(maybe_addr.error());
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return err<tcp_stream>(error::from_errno());
    }

    unique_fd owned_fd{fd};
    auto addr = maybe_addr.value();
    if (::connect(owned_fd.get(), reinterpret_cast<const sockaddr *>(&addr),
                  sizeof(addr)) != 0) {
        return err<tcp_stream>(error::from_errno());
    }

    return tcp_stream{std::move(owned_fd)};
}

result<std::size_t>
tcp_stream::read_some(std::span<std::byte> buffer) noexcept {
    if (!valid()) {
        return err<std::size_t>(make_error_from_errno(EBADF));
    }

    if (buffer.empty()) {
        return static_cast<std::size_t>(0);
    }

    const ssize_t read_count =
        ::recv(fd_.get(), buffer.data(), buffer.size(), 0);
    if (read_count < 0) {
        return err<std::size_t>(error::from_errno());
    }

    return static_cast<std::size_t>(read_count);
}

result<std::size_t>
tcp_stream::write_some(std::span<const std::byte> buffer) noexcept {
    if (!valid()) {
        return err<std::size_t>(make_error_from_errno(EBADF));
    }

    if (buffer.empty()) {
        return static_cast<std::size_t>(0);
    }

    const ssize_t write_count =
        ::send(fd_.get(), buffer.data(), buffer.size(), MSG_NOSIGNAL);
    if (write_count < 0) {
        return err<std::size_t>(error::from_errno());
    }

    return static_cast<std::size_t>(write_count);
}

result<void> tcp_stream::shutdown_write() noexcept {
    if (!valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }

    if (::shutdown(fd_.get(), SHUT_WR) == 0) {
        return ok();
    }
    return err<void>(error::from_errno());
}

int tcp_stream::native_handle() const noexcept {
    return fd_.get();
}

bool tcp_stream::valid() const noexcept {
    return fd_.valid();
}

tcp_listener::tcp_listener(simplenet::unique_fd fd) noexcept : fd_(std::move(fd)) {}

result<tcp_listener> tcp_listener::bind(const endpoint& local,
                                        int backlog) noexcept {
    const auto maybe_addr = detail::to_sockaddr(local);
    if (!maybe_addr.has_value()) {
        return err<tcp_listener>(maybe_addr.error());
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return err<tcp_listener>(error::from_errno());
    }

    unique_fd owned_fd{fd};
    const auto reuse_status = detail::set_reuse_addr(owned_fd.get());
    if (!reuse_status.has_value()) {
        return err<tcp_listener>(reuse_status.error());
    }

    auto addr = maybe_addr.value();
    if (::bind(owned_fd.get(), reinterpret_cast<const sockaddr *>(&addr),
               sizeof(addr)) != 0) {
        return err<tcp_listener>(error::from_errno());
    }

    if (::listen(owned_fd.get(), backlog) != 0) {
        return err<tcp_listener>(error::from_errno());
    }

    return tcp_listener{std::move(owned_fd)};
}

result<tcp_stream> tcp_listener::accept() noexcept {
    if (!valid()) {
        return err<tcp_stream>(make_error_from_errno(EBADF));
    }

    const int accepted = ::accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC);
    if (accepted < 0) {
        return err<tcp_stream>(error::from_errno());
    }

    return tcp_stream{unique_fd{accepted}};
}

result<std::uint16_t> tcp_listener::local_port() const noexcept {
    if (!valid()) {
        return err<std::uint16_t>(make_error_from_errno(EBADF));
    }

    sockaddr_in addr{};
    auto addr_len = static_cast<socklen_t>(sizeof(addr));
    if (::getsockname(fd_.get(), reinterpret_cast<sockaddr *>(&addr),
                      &addr_len) != 0) {
        return err<std::uint16_t>(error::from_errno());
    }

    return ntohs(addr.sin_port);
}

int tcp_listener::native_handle() const noexcept {
    return fd_.get();
}

bool tcp_listener::valid() const noexcept {
    return fd_.valid();
}

result<void> write_all(tcp_stream& stream,
                       std::span<const std::byte> buffer) noexcept {
    if (!stream.valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }
    if (buffer.empty()) {
        return ok();
    }

    const int fd = stream.native_handle();
    const std::byte* ptr = buffer.data();
    std::size_t remaining = buffer.size();
    while (remaining > 0) {
        const ssize_t write_count =
            ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (write_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return err<void>(error::from_errno());
        }
        if (write_count == 0) {
            return err<void>(make_error_from_errno(EPIPE));
        }
        const auto advanced = static_cast<std::size_t>(write_count);
        ptr += advanced;
        remaining -= advanced;
    }

    return ok();
}

result<void> read_exact(tcp_stream& stream,
                        std::span<std::byte> buffer) noexcept {
    if (!stream.valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }
    if (buffer.empty()) {
        return ok();
    }

    const int fd = stream.native_handle();
    std::byte* ptr = buffer.data();
    std::size_t remaining = buffer.size();
    while (remaining > 0) {
        const ssize_t read_count = ::recv(fd, ptr, remaining, 0);
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return err<void>(error::from_errno());
        }
        if (read_count == 0) {
            return err<void>(make_error_from_errno(ECONNRESET));
        }
        const auto advanced = static_cast<std::size_t>(read_count);
        ptr += advanced;
        remaining -= advanced;
    }

    return ok();
}

} // namespace simplenet::blocking
