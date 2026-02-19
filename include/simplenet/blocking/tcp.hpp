#pragma once

/**
 * @file
 * @brief Blocking TCP stream and listener APIs.
 */

#include "simplenet/blocking/endpoint.hpp"
#include "simplenet/core/result.hpp"
#include "simplenet/core/unique_fd.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace simplenet::blocking {

/**
 * @brief Blocking TCP connected socket.
 */
class tcp_stream {
public:
    /// Construct an empty stream.
    tcp_stream() noexcept = default;
    /// Construct from an already-open connected socket.
    explicit tcp_stream(simplenet::unique_fd fd) noexcept;

    tcp_stream(const tcp_stream&) = delete;
    tcp_stream& operator=(const tcp_stream&) = delete;
    tcp_stream(tcp_stream&&) noexcept = default;
    tcp_stream& operator=(tcp_stream&&) noexcept = default;

    /**
     * @brief Connect to a remote endpoint.
     * @param remote Remote host/port.
     */
    [[nodiscard]] static result<tcp_stream>
    connect(const endpoint& remote) noexcept;

    /**
     * @brief Read up to `buffer.size()` bytes.
     * @param buffer Output byte span.
     * @return Number of bytes read, or 0 on peer shutdown.
     */
    [[nodiscard]] result<std::size_t>
    read_some(std::span<std::byte> buffer) noexcept;
    /**
     * @brief Write up to `buffer.size()` bytes.
     * @param buffer Input byte span.
     * @return Number of bytes written.
     */
    [[nodiscard]] result<std::size_t>
    write_some(std::span<const std::byte> buffer) noexcept;
    /// @brief Shutdown the write half of the connection.
    [[nodiscard]] result<void> shutdown_write() noexcept;

    /// @return Native socket descriptor.
    [[nodiscard]] int native_handle() const noexcept;
    /// @return `true` when a valid socket is owned.
    [[nodiscard]] bool valid() const noexcept;

private:
    simplenet::unique_fd fd_;
};

/**
 * @brief Blocking TCP listening socket.
 */
class tcp_listener {
public:
    /// Construct an empty listener.
    tcp_listener() noexcept = default;
    /// Construct from an already-open listening socket.
    explicit tcp_listener(simplenet::unique_fd fd) noexcept;

    tcp_listener(const tcp_listener&) = delete;
    tcp_listener& operator=(const tcp_listener&) = delete;
    tcp_listener(tcp_listener&&) noexcept = default;
    tcp_listener& operator=(tcp_listener&&) noexcept = default;

    /**
     * @brief Bind and listen on a local endpoint.
     * @param local Local host/port.
     * @param backlog Kernel listen backlog.
     */
    [[nodiscard]] static result<tcp_listener> bind(const endpoint& local,
                                                   int backlog = 128) noexcept;
    /**
     * @brief Accept a single incoming connection.
     * @return Connected stream socket.
     */
    [[nodiscard]] result<tcp_stream> accept() noexcept;
    /// @return Bound local port number.
    [[nodiscard]] result<std::uint16_t> local_port() const noexcept;

    /// @return Native listening socket descriptor.
    [[nodiscard]] int native_handle() const noexcept;
    /// @return `true` when a valid socket is owned.
    [[nodiscard]] bool valid() const noexcept;

private:
    simplenet::unique_fd fd_;
};

/**
 * @brief Keep writing until the entire buffer is transferred.
 * @param stream Connected stream.
 * @param buffer Bytes to send.
 */
[[nodiscard]] result<void>
write_all(tcp_stream& stream, std::span<const std::byte> buffer) noexcept;
/**
 * @brief Keep reading until the entire buffer is filled.
 * @param stream Connected stream.
 * @param buffer Destination bytes.
 */
[[nodiscard]] result<void> read_exact(tcp_stream& stream,
                                      std::span<std::byte> buffer) noexcept;

} // namespace simplenet::blocking
