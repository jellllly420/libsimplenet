#pragma once

/**
 * @file
 * @brief Lightweight error wrapper for library-wide error propagation.
 */

#include <cerrno>
#include <string>
#include <system_error>

namespace simplenet {

/**
 * @brief Error value used across `result<T>`.
 *
 * This type wraps `std::error_code` while providing helper constructors
 * for errno-based failures.
 */
class error {
public:
    /// Construct a success-like empty error (`value() == 0`).
    error() noexcept = default;
    /// Construct from an explicit error code.
    explicit error(std::error_code code) noexcept;

    /**
     * @brief Build an error from errno.
     * @param value errno value. Defaults to current `errno`.
     * @return Converted `error` in the generic category.
     */
    [[nodiscard]] static error from_errno(int value = errno) noexcept;

    /// @return Underlying `std::error_code`.
    [[nodiscard]] std::error_code code() const noexcept;
    /// @return Integer code value.
    [[nodiscard]] int value() const noexcept;
    /// @return Human-readable message for the code.
    [[nodiscard]] std::string message() const;

private:
    std::error_code code_;
};

/**
 * @brief Convenience helper that wraps an errno value into `error`.
 * @param value errno value to convert.
 */
[[nodiscard]] error make_error_from_errno(int value) noexcept;

} // namespace simplenet
