#pragma once

/**
 * @file
 * @brief Backpressure-aware queued TCP writer for async pipelines.
 */

#include "simplenet/nonblocking/tcp.hpp"
#include "simplenet/runtime/cancel.hpp"
#include "simplenet/runtime/io_ops.hpp"
#include "simplenet/runtime/task.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace simplenet::runtime {

/**
 * @brief Queue size thresholds for backpressure signaling.
 */
struct watermarks {
    /// Threshold that clears high-watermark state.
    std::size_t low{64U * 1024U};
    /// Threshold that activates high-watermark state.
    std::size_t high{256U * 1024U};
};

/// @brief Logical backpressure state returned by enqueue operations.
enum class backpressure_state {
    normal = 0,
    high_watermark = 1,
};

/**
 * @brief Buffered async TCP writer with explicit backpressure reporting.
 */
class queued_writer {
public:
    /**
     * @brief Construct from an owned stream and watermark settings.
     * @param stream Destination stream.
     * @param marks Low/high watermark values.
     */
    explicit queued_writer(simplenet::nonblocking::tcp_stream stream,
                           watermarks marks = {});

    queued_writer(const queued_writer&) = delete;
    queued_writer& operator=(const queued_writer&) = delete;
    queued_writer(queued_writer&&) noexcept = default;
    queued_writer& operator=(queued_writer&&) noexcept = default;

    /**
     * @brief Copy-enqueue bytes for later flush.
     * @param bytes Bytes to append into internal queue.
     * @return Backpressure state after enqueue.
     */
    [[nodiscard]] result<backpressure_state>
    enqueue(std::span<const std::byte> bytes);
    /**
     * @brief Move-enqueue an owned byte vector without extra copy.
     * @param bytes Buffer to transfer into internal queue.
     * @return Backpressure state after enqueue.
     */
    [[nodiscard]] result<backpressure_state>
    enqueue(std::vector<std::byte>&& bytes);
    /**
     * @brief Flush queued buffers with timeout and optional cancellation.
     * @param timeout Per-operation timeout for async writes.
     * @param token Optional cancellation token.
     */
    [[nodiscard]] task<result<void>> flush(std::chrono::milliseconds timeout,
                                           cancel_token token = {});
    /**
     * @brief Flush queue then shutdown stream write side.
     * @param timeout Per-operation timeout for flush/write.
     * @param token Optional cancellation token.
     */
    [[nodiscard]] task<result<void>>
    graceful_shutdown(std::chrono::milliseconds timeout,
                      cancel_token token = {});

    /// @return Total bytes currently buffered.
    [[nodiscard]] std::size_t queued_bytes() const noexcept;
    /// @return Whether high-watermark state is currently active.
    [[nodiscard]] bool high_watermark_active() const noexcept;
    /// @return Underlying socket descriptor.
    [[nodiscard]] int native_handle() const noexcept;

private:
    [[nodiscard]] result<backpressure_state>
    enqueue_owned(std::vector<std::byte>&& bytes);
    void update_backpressure_after_drain() noexcept;

    simplenet::nonblocking::tcp_stream stream_{};
    watermarks marks_{};
    std::deque<std::vector<std::byte>> queue_{};
    std::size_t front_offset_{0};
    std::size_t queued_bytes_{0};
    bool high_watermark_active_{false};
};

} // namespace simplenet::runtime
