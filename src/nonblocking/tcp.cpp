#include "simplenet/nonblocking/tcp.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

simplenet::result<sockaddr_in>
to_sockaddr(const simplenet::nonblocking::endpoint& ep) noexcept {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(ep.port);

    const int parsed = ::inet_pton(AF_INET, ep.host.c_str(), &addr.sin_addr);
    if (parsed == 1) {
        return addr;
    }
    return simplenet::err<sockaddr_in>(simplenet::make_error_from_errno(EINVAL));
}

simplenet::result<void> set_reuse_addr(int fd) noexcept {
    int enabled = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ==
        0) {
        return simplenet::ok();
    }
    return simplenet::err<void>(simplenet::error::from_errno());
}

simplenet::result<int> make_stream_socket_nonblocking() noexcept {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd >= 0) {
        return fd;
    }

    fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return simplenet::err<int>(simplenet::error::from_errno());
    }

    const auto status = simplenet::nonblocking::set_nonblocking(fd);
    if (!status.has_value()) {
        (void)::close(fd);
        return simplenet::err<int>(status.error());
    }

    return fd;
}

} // namespace

namespace simplenet::nonblocking {

tcp_stream::tcp_stream(simplenet::unique_fd fd) noexcept : fd_(std::move(fd)) {}

result<tcp_stream> tcp_stream::connect(const endpoint& remote) noexcept {
    const auto maybe_addr = to_sockaddr(remote);
    if (!maybe_addr.has_value()) {
        return err<tcp_stream>(maybe_addr.error());
    }

    const auto maybe_fd = make_stream_socket_nonblocking();
    if (!maybe_fd.has_value()) {
        return err<tcp_stream>(maybe_fd.error());
    }

    unique_fd owned_fd{maybe_fd.value()};
    auto addr = maybe_addr.value();
    if (::connect(owned_fd.get(), reinterpret_cast<const sockaddr *>(&addr),
                  sizeof(addr)) == 0) {
        return tcp_stream{std::move(owned_fd)};
    }

    if (errno == EINPROGRESS) {
        return tcp_stream{std::move(owned_fd)};
    }
    return err<tcp_stream>(error::from_errno());
}

result<void> tcp_stream::finish_connect() noexcept {
    if (!valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }

    int socket_error = 0;
    auto error_len = static_cast<socklen_t>(sizeof(socket_error));
    if (::getsockopt(fd_.get(), SOL_SOCKET, SO_ERROR, &socket_error,
                     &error_len) != 0) {
        return err<void>(error::from_errno());
    }
    if (socket_error == 0) {
        return ok();
    }
    return err<void>(make_error_from_errno(socket_error));
}

result<std::size_t>
tcp_stream::read_some(std::span<std::byte> buffer) noexcept {
    if (!valid()) {
        return err<std::size_t>(make_error_from_errno(EBADF));
    }
    if (buffer.empty()) {
        return static_cast<std::size_t>(0);
    }

    const ssize_t count = ::recv(fd_.get(), buffer.data(), buffer.size(), 0);
    if (count < 0) {
        return err<std::size_t>(error::from_errno());
    }
    return static_cast<std::size_t>(count);
}

result<std::size_t>
tcp_stream::write_some(std::span<const std::byte> buffer) noexcept {
    if (!valid()) {
        return err<std::size_t>(make_error_from_errno(EBADF));
    }
    if (buffer.empty()) {
        return static_cast<std::size_t>(0);
    }

    const ssize_t count =
        ::send(fd_.get(), buffer.data(), buffer.size(), MSG_NOSIGNAL);
    if (count < 0) {
        return err<std::size_t>(error::from_errno());
    }
    return static_cast<std::size_t>(count);
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

result<void> tcp_stream::set_send_buffer_size(int bytes) noexcept {
    if (!valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }
    if (bytes <= 0) {
        return err<void>(make_error_from_errno(EINVAL));
    }

    if (::setsockopt(fd_.get(), SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) ==
        0) {
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
    const auto maybe_addr = to_sockaddr(local);
    if (!maybe_addr.has_value()) {
        return err<tcp_listener>(maybe_addr.error());
    }

    const auto maybe_fd = make_stream_socket_nonblocking();
    if (!maybe_fd.has_value()) {
        return err<tcp_listener>(maybe_fd.error());
    }

    unique_fd owned_fd{maybe_fd.value()};
    const auto reuse_status = set_reuse_addr(owned_fd.get());
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

    const int accepted =
        ::accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (accepted < 0) {
        return err<tcp_stream>(error::from_errno());
    }
    return tcp_stream{simplenet::unique_fd{accepted}};
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

result<void> set_nonblocking(int fd) noexcept {
    if (fd < 0) {
        return err<void>(make_error_from_errno(EBADF));
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return err<void>(error::from_errno());
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0) {
        return ok();
    }
    return err<void>(error::from_errno());
}

bool is_would_block(const simplenet::error& err) noexcept {
    return err.value() == EAGAIN || err.value() == EWOULDBLOCK;
}

bool is_in_progress(const simplenet::error& err) noexcept {
    return err.value() == EINPROGRESS;
}

} // namespace simplenet::nonblocking
