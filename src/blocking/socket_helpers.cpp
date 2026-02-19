#include "socket_helpers.hpp"

#include <arpa/inet.h>
#include <array>
#include <sys/socket.h>

namespace simplenet::blocking::detail {

result<sockaddr_in> to_sockaddr(const endpoint& ep) noexcept {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(ep.port);

    const int parse_status =
        ::inet_pton(AF_INET, ep.host.c_str(), &addr.sin_addr);
    if (parse_status == 1) {
        return addr;
    }

    return err<sockaddr_in>(make_error_from_errno(EINVAL));
}

result<endpoint> from_sockaddr(const sockaddr_in& addr) noexcept {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    const char *converted =
        ::inet_ntop(AF_INET, &addr.sin_addr, buffer.data(), buffer.size());
    if (converted == nullptr) {
        return err<endpoint>(error::from_errno());
    }

    return endpoint{.host = std::string{converted},
                    .port = ntohs(addr.sin_port)};
}

result<void> set_reuse_addr(int fd) noexcept {
    int enabled = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ==
        0) {
        return ok();
    }
    return err<void>(error::from_errno());
}

} // namespace simplenet::blocking::detail
