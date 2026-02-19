#pragma once

/**
 * @file
 * @brief Endpoint primitives for host/port socket addressing.
 */

#include <cstdint>
#include <string>

namespace simplenet::blocking {

/**
 * @brief IPv4 endpoint represented as textual host and TCP/UDP port.
 */
struct endpoint {
    /// Hostname or IPv4 literal.
    std::string host;
    /// Network port in host byte order.
    std::uint16_t port{};

    /**
     * @brief Create a loopback endpoint (`127.0.0.1:port`).
     * @param port Destination/listen port.
     */
    [[nodiscard]] static endpoint loopback(std::uint16_t port);
    /**
     * @brief Create an any-address endpoint (`0.0.0.0:port`).
     * @param port Destination/listen port.
     */
    [[nodiscard]] static endpoint any(std::uint16_t port);
};

} // namespace simplenet::blocking
