#pragma once

#include "simplenet/blocking/endpoint.hpp"
#include "simplenet/core/result.hpp"

#include <netinet/in.h>

namespace simplenet::blocking::detail {

[[nodiscard]] result<sockaddr_in> to_sockaddr(const endpoint& ep) noexcept;
[[nodiscard]] result<endpoint> from_sockaddr(const sockaddr_in& addr) noexcept;
[[nodiscard]] result<void> set_reuse_addr(int fd) noexcept;

} // namespace simplenet::blocking::detail
