#pragma once

/**
 * @file
 * @brief RAII ownership wrapper for POSIX file descriptors.
 */

#include "simplenet/core/result.hpp"

namespace simplenet {

/**
 * @brief Move-only owner of a file descriptor.
 */
class unique_fd {
public:
    /// Construct an empty handle (`fd == -1`).
    unique_fd() noexcept = default;
    /// Take ownership of an existing descriptor.
    explicit unique_fd(int fd) noexcept;
    /// Close the descriptor if still owned.
    ~unique_fd() noexcept;

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

    /// Move ownership from another instance.
    unique_fd(unique_fd&& other) noexcept;
    /// Move-assign ownership from another instance.
    unique_fd& operator=(unique_fd&& other) noexcept;

    /// @return Owned file descriptor or `-1`.
    [[nodiscard]] int get() const noexcept;
    /// @return `true` when the object owns a valid descriptor.
    [[nodiscard]] bool valid() const noexcept;
    /// @return Same as `valid()`.
    [[nodiscard]] explicit operator bool() const noexcept;

    /**
     * @brief Release ownership without closing.
     * @return Previously owned descriptor or `-1`.
     */
    [[nodiscard]] int release() noexcept;
    /**
     * @brief Replace the owned descriptor.
     * @param fd New descriptor. Defaults to `-1` (close and clear).
     */
    void reset(int fd = -1) noexcept;
    /// Swap ownership with another instance.
    void swap(unique_fd& other) noexcept;

private:
    int fd_{-1};
};

/**
 * @brief Close a descriptor and convert errno to `result<void>`.
 * @param fd File descriptor to close.
 */
[[nodiscard]] result<void> close_fd(int fd) noexcept;

} // namespace simplenet
