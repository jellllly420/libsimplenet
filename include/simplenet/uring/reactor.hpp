#pragma once

/**
 * @file
 * @brief Thin `io_uring` wrapper used by the runtime scheduler.
 */

#include "simplenet/core/result.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <liburing.h>
#include <memory>
#include <optional>
#include <span>

namespace simplenet::uring {

/**
 * @brief One completion queue entry snapshot.
 */
struct completion {
    /// User token attached when submitting.
    std::uint64_t user_data{0};
    /// Kernel completion result (`res`).
    int result{0};
};

/**
 * @brief RAII wrapper over a configured `io_uring` ring.
 */
class reactor {
public:
    /// Construct an invalid reactor.
    reactor() noexcept = default;
    /// Construct from an initialized ring object.
    explicit reactor(
        std::unique_ptr<io_uring, void (*)(io_uring *)> ring) noexcept;

    reactor(const reactor&) = delete;
    reactor& operator=(const reactor&) = delete;
    reactor(reactor&&) noexcept = default;
    reactor& operator=(reactor&&) noexcept = default;

    /**
     * @brief Create and initialize an `io_uring` instance.
     * @param entries Ring queue depth.
     */
    [[nodiscard]] static result<reactor>
    create(std::uint32_t entries = 256) noexcept;

    /**
     * @brief Queue a poll-add request.
     * @param user_data Completion token.
     * @param fd Descriptor to monitor.
     * @param poll_mask Poll mask (`POLLIN`, `POLLOUT`, ...).
     */
    [[nodiscard]] result<void>
    submit_poll_add(std::uint64_t user_data, int fd,
                    std::uint32_t poll_mask) noexcept;
    /**
     * @brief Queue a poll-remove request.
     * @param target_user_data Token of the poll-add to cancel.
     */
    [[nodiscard]] result<void>
    submit_poll_remove(std::uint64_t target_user_data) noexcept;
    /// @brief Submit pending SQEs to the kernel.
    [[nodiscard]] result<void> submit() noexcept;
    /**
     * @brief Wait for completion events.
     * @param completions Output span for completions.
     * @param timeout Optional timeout. Empty means block indefinitely.
     * @return Number of completions written.
     */
    [[nodiscard]] result<std::size_t>
    wait(std::span<completion> completions,
         std::optional<std::chrono::milliseconds> timeout) noexcept;

    /// @return `true` when the ring is initialized.
    [[nodiscard]] bool valid() const noexcept;

private:
    std::unique_ptr<io_uring, void (*)(io_uring *)> ring_{nullptr, nullptr};
};

} // namespace simplenet::uring
