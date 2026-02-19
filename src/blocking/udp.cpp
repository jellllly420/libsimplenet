#include "simplenet/blocking/udp.hpp"

#include "socket_helpers.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>

namespace simplenet::blocking {

udp_socket::udp_socket(simplenet::unique_fd fd) noexcept : fd_(std::move(fd)) {}

result<udp_socket> udp_socket::bind(const endpoint& local) noexcept {
    const auto maybe_addr = detail::to_sockaddr(local);
    if (!maybe_addr.has_value()) {
        return err<udp_socket>(maybe_addr.error());
    }

    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return err<udp_socket>(error::from_errno());
    }

    unique_fd owned_fd{fd};
    const auto reuse_status = detail::set_reuse_addr(owned_fd.get());
    if (!reuse_status.has_value()) {
        return err<udp_socket>(reuse_status.error());
    }

    auto addr = maybe_addr.value();
    if (::bind(owned_fd.get(), reinterpret_cast<const sockaddr *>(&addr),
               sizeof(addr)) != 0) {
        return err<udp_socket>(error::from_errno());
    }

    return udp_socket{std::move(owned_fd)};
}

result<std::size_t> udp_socket::send_to(std::span<const std::byte> buffer,
                                        const endpoint& remote) noexcept {
    if (!valid()) {
        return err<std::size_t>(make_error_from_errno(EBADF));
    }

    const auto maybe_addr = detail::to_sockaddr(remote);
    if (!maybe_addr.has_value()) {
        return err<std::size_t>(maybe_addr.error());
    }

    auto addr = maybe_addr.value();
    const ssize_t sent =
        ::sendto(fd_.get(), buffer.data(), buffer.size(), MSG_NOSIGNAL,
                 reinterpret_cast<const sockaddr *>(&addr),
                 static_cast<socklen_t>(sizeof(addr)));

    if (sent < 0) {
        return err<std::size_t>(error::from_errno());
    }
    return static_cast<std::size_t>(sent);
}

result<received_datagram>
udp_socket::recv_from(std::span<std::byte> buffer) noexcept {
    if (!valid()) {
        return err<received_datagram>(make_error_from_errno(EBADF));
    }

    if (buffer.empty()) {
        return err<received_datagram>(make_error_from_errno(EINVAL));
    }

    sockaddr_in from_addr{};
    auto from_len = static_cast<socklen_t>(sizeof(from_addr));

    const ssize_t received =
        ::recvfrom(fd_.get(), buffer.data(), buffer.size(), 0,
                   reinterpret_cast<sockaddr *>(&from_addr), &from_len);

    if (received < 0) {
        return err<received_datagram>(error::from_errno());
    }

    const auto from_endpoint = detail::from_sockaddr(from_addr);
    if (!from_endpoint.has_value()) {
        return err<received_datagram>(from_endpoint.error());
    }

    return received_datagram{.size = static_cast<std::size_t>(received),
                             .from = from_endpoint.value()};
}

result<std::uint16_t> udp_socket::local_port() const noexcept {
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

int udp_socket::native_handle() const noexcept {
    return fd_.get();
}

bool udp_socket::valid() const noexcept {
    return fd_.valid();
}

} // namespace simplenet::blocking
