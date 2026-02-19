#pragma once

/**
 * @file
 * @brief Blocking UDP socket APIs.
 */

#include "simplenet/blocking/endpoint.hpp"
#include "simplenet/core/result.hpp"
#include "simplenet/core/unique_fd.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace simplenet::blocking {

/**
 * @brief Metadata returned by UDP receive operations.
 */
struct received_datagram {
    /// Number of bytes copied into caller buffer.
    std::size_t size{};
    /// Sender endpoint.
    endpoint from{};
};

/**
 * @brief Blocking UDP datagram socket.
 */
class udp_socket {
public:
    /// Construct an empty socket.
    udp_socket() noexcept = default;
    /// Construct from an already-open datagram socket.
    explicit udp_socket(simplenet::unique_fd fd) noexcept;

    udp_socket(const udp_socket&) = delete;
    udp_socket& operator=(const udp_socket&) = delete;
    udp_socket(udp_socket&&) noexcept = default;
    udp_socket& operator=(udp_socket&&) noexcept = default;

    /**
     * @brief Bind to a local endpoint.
     * @param local Host/port to bind.
     */
    [[nodiscard]] static result<udp_socket>
    bind(const endpoint& local) noexcept;
    /**
     * @brief Send a datagram to a remote endpoint.
     * @param buffer Bytes to send.
     * @param remote Destination endpoint.
     */
    [[nodiscard]] result<std::size_t> send_to(std::span<const std::byte> buffer,
                                              const endpoint& remote) noexcept;
    /**
     * @brief Receive a datagram.
     * @param buffer Destination bytes.
     * @return Size and source endpoint metadata.
     */
    [[nodiscard]] result<received_datagram>
    recv_from(std::span<std::byte> buffer) noexcept;
    /// @return Bound local port number.
    [[nodiscard]] result<std::uint16_t> local_port() const noexcept;

    /// @return Native socket descriptor.
    [[nodiscard]] int native_handle() const noexcept;
    /// @return `true` when a valid socket is owned.
    [[nodiscard]] bool valid() const noexcept;

private:
    simplenet::unique_fd fd_;
};

} // namespace simplenet::blocking
