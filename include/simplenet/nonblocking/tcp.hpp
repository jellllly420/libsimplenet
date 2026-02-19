#pragma once

/**
 * @file
 * @brief Nonblocking TCP socket primitives used by async runtime APIs.
 */

#include "simplenet/blocking/endpoint.hpp"
#include "simplenet/core/result.hpp"
#include "simplenet/core/unique_fd.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace simplenet::nonblocking {

/// Alias to the shared endpoint type.
using endpoint = simplenet::blocking::endpoint;

/**
 * @brief Nonblocking connected TCP socket.
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
     * @brief Create a socket and start a nonblocking connect.
     * @param remote Destination endpoint.
     */
    [[nodiscard]] static result<tcp_stream>
    connect(const endpoint& remote) noexcept;
    /// @brief Complete a pending nonblocking connect.
    [[nodiscard]] result<void> finish_connect() noexcept;
    /// @brief Read available bytes without blocking.
    [[nodiscard]] result<std::size_t>
    read_some(std::span<std::byte> buffer) noexcept;
    /// @brief Write available bytes without blocking.
    [[nodiscard]] result<std::size_t>
    write_some(std::span<const std::byte> buffer) noexcept;
    /// @brief Shutdown the write half of the connection.
    [[nodiscard]] result<void> shutdown_write() noexcept;
    /**
     * @brief Tune kernel send-buffer size.
     * @param bytes Requested SO_SNDBUF value.
     */
    [[nodiscard]] result<void> set_send_buffer_size(int bytes) noexcept;

    /// @return Native socket descriptor.
    [[nodiscard]] int native_handle() const noexcept;
    /// @return `true` when a valid socket is owned.
    [[nodiscard]] bool valid() const noexcept;

private:
    simplenet::unique_fd fd_{};
};

/**
 * @brief Nonblocking TCP listening socket.
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
    /// @brief Accept one connection without blocking.
    [[nodiscard]] result<tcp_stream> accept() noexcept;
    /// @return Bound local port number.
    [[nodiscard]] result<std::uint16_t> local_port() const noexcept;

    /// @return Native listening socket descriptor.
    [[nodiscard]] int native_handle() const noexcept;
    /// @return `true` when a valid socket is owned.
    [[nodiscard]] bool valid() const noexcept;

private:
    simplenet::unique_fd fd_{};
};

/**
 * @brief Put a descriptor into nonblocking mode.
 * @param fd Descriptor to update.
 */
[[nodiscard]] result<void> set_nonblocking(int fd) noexcept;
/**
 * @brief Test whether an error represents "operation would block".
 * @param err Error value to inspect.
 */
[[nodiscard]] bool is_would_block(const simplenet::error& err) noexcept;
/**
 * @brief Test whether an error represents "operation in progress".
 * @param err Error value to inspect.
 */
[[nodiscard]] bool is_in_progress(const simplenet::error& err) noexcept;

} // namespace simplenet::nonblocking
