#pragma once

/**
 * @file
 * @brief Thin epoll wrapper used by the runtime scheduler.
 */

#include "simplenet/core/result.hpp"
#include "simplenet/core/unique_fd.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace simplenet::epoll {

/**
 * @brief One `epoll_wait` readiness event.
 */
struct ready_event {
    /// Ready file descriptor.
    int fd{-1};
    /// Ready bitmask (`EPOLLIN`, `EPOLLOUT`, ...).
    std::uint32_t events{0};
};

/**
 * @brief RAII wrapper over an epoll instance.
 */
class reactor {
public:
    /// Construct an invalid reactor.
    reactor() noexcept = default;
    /// Construct from an existing epoll descriptor.
    explicit reactor(simplenet::unique_fd epoll_fd) noexcept;

    reactor(const reactor&) = delete;
    reactor& operator=(const reactor&) = delete;
    reactor(reactor&&) noexcept = default;
    reactor& operator=(reactor&&) noexcept = default;

    /// @brief Create a new epoll instance.
    [[nodiscard]] static result<reactor> create() noexcept;
    /// @brief Register descriptor interest.
    [[nodiscard]] result<void> add(int fd, std::uint32_t events) noexcept;
    /// @brief Modify descriptor interest mask.
    [[nodiscard]] result<void> modify(int fd, std::uint32_t events) noexcept;
    /// @brief Remove descriptor from epoll.
    [[nodiscard]] result<void> remove(int fd) noexcept;
    /**
     * @brief Wait for readiness events.
     * @param events Output span of event slots.
     * @param timeout Maximum wait duration.
     * @return Number of filled event entries.
     */
    [[nodiscard]] result<std::size_t>
    wait(std::span<ready_event> events,
         std::chrono::milliseconds timeout) noexcept;

    /// @return Native epoll descriptor.
    [[nodiscard]] int native_handle() const noexcept;
    /// @return `true` when a valid epoll descriptor is owned.
    [[nodiscard]] bool valid() const noexcept;

private:
    [[nodiscard]] result<void> ctl(int operation, int fd, std::uint32_t events,
                                   bool has_event) noexcept;

    simplenet::unique_fd epoll_fd_;
};

/**
 * @brief Check whether a specific flag is present in an event mask.
 * @param event_mask Mask to inspect.
 * @param flag Event flag.
 */
[[nodiscard]] bool has_event(std::uint32_t event_mask,
                             std::uint32_t flag) noexcept;

} // namespace simplenet::epoll
