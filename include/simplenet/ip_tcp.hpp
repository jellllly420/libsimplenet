#pragma once

/**
 * @file
 * @brief TCP protocol façade aliases, similar to Boost.Asio's `ip::tcp`.
 */

#include "simplenet/blocking/endpoint.hpp"
#include "simplenet/nonblocking/tcp.hpp"

namespace simplenet::ip {

/**
 * @brief Protocol tag that exposes canonical TCP endpoint/socket types.
 */
struct tcp {
    /// TCP address/port pair.
    using endpoint = simplenet::blocking::endpoint;
    /// Nonblocking stream socket type.
    using socket = simplenet::nonblocking::tcp_stream;
    /// Nonblocking listening socket type.
    using acceptor = simplenet::nonblocking::tcp_listener;
};

} // namespace simplenet::ip
