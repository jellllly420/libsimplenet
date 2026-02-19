#pragma once

/**
 * @file
 * @brief Coroutine-based async I/O operations built on `scheduler`.
 */

#include "simplenet/nonblocking/tcp.hpp"
#include "simplenet/runtime/cancel.hpp"
#include "simplenet/runtime/task.hpp"

#include <chrono>
#include <cstddef>
#include <span>

namespace simplenet::runtime {

/// @brief Suspend until the descriptor is readable.
[[nodiscard]] task<result<void>> wait_readable(int fd);
/// @brief Suspend until the descriptor is writable.
[[nodiscard]] task<result<void>> wait_writable(int fd);
/// @brief Suspend until readable or timeout.
[[nodiscard]] task<result<void>>
wait_readable_for(int fd, std::chrono::milliseconds timeout);
/// @brief Suspend until writable or timeout.
[[nodiscard]] task<result<void>>
wait_writable_for(int fd, std::chrono::milliseconds timeout);

/// @brief Accept one TCP connection asynchronously.
[[nodiscard]] task<result<simplenet::nonblocking::tcp_stream>>
async_accept(simplenet::nonblocking::tcp_listener& listener);
/// @brief Connect to a remote endpoint asynchronously.
[[nodiscard]] task<result<simplenet::nonblocking::tcp_stream>>
async_connect(const simplenet::nonblocking::endpoint& endpoint);

/// @brief Read available bytes from a stream asynchronously.
[[nodiscard]] task<result<std::size_t>>
async_read_some(simplenet::nonblocking::tcp_stream& stream,
                std::span<std::byte> buffer);
/// @brief Write available bytes to a stream asynchronously.
[[nodiscard]] task<result<std::size_t>>
async_write_some(simplenet::nonblocking::tcp_stream& stream,
                 std::span<const std::byte> buffer);

/// @brief Read exactly `buffer.size()` bytes unless an error occurs.
[[nodiscard]] task<result<void>>
async_read_exact(simplenet::nonblocking::tcp_stream& stream,
                 std::span<std::byte> buffer);
/// @brief Write exactly `buffer.size()` bytes unless an error occurs.
[[nodiscard]] task<result<void>>
async_write_all(simplenet::nonblocking::tcp_stream& stream,
                std::span<const std::byte> buffer);

/**
 * @brief Asynchronous sleep with optional cancellation.
 * @param duration Sleep duration.
 * @param token Optional cancellation token.
 */
[[nodiscard]] task<result<void>> async_sleep(std::chrono::milliseconds duration,
                                             cancel_token token = {});

/**
 * @brief Read with timeout and optional cancellation.
 * @param stream Stream to read from.
 * @param buffer Destination bytes.
 * @param timeout Timeout bound.
 * @param token Optional cancellation token.
 */
[[nodiscard]] task<result<std::size_t>> async_read_some_with_timeout(
    simplenet::nonblocking::tcp_stream& stream, std::span<std::byte> buffer,
    std::chrono::milliseconds timeout, cancel_token token = {});
/**
 * @brief Write with timeout and optional cancellation.
 * @param stream Stream to write to.
 * @param buffer Source bytes.
 * @param timeout Timeout bound.
 * @param token Optional cancellation token.
 */
[[nodiscard]] task<result<std::size_t>> async_write_some_with_timeout(
    simplenet::nonblocking::tcp_stream& stream, std::span<const std::byte> buffer,
    std::chrono::milliseconds timeout, cancel_token token = {});

} // namespace simplenet::runtime
