#pragma once

/**
 * @file
 * @brief Result alias and helper constructors based on `std::expected`.
 */

#include "simplenet/core/error.hpp"

#include <expected>
#include <type_traits>
#include <utility>

namespace simplenet {

/**
 * @brief Standard operation result type used by the library.
 * @tparam T Success value type.
 */
template <class T>
using result = std::expected<T, error>;

/**
 * @brief Construct a successful `result<T>`.
 * @tparam T Value type.
 * @param value Value to store in the success state.
 */
template <class T>
[[nodiscard]] constexpr result<std::decay_t<T>> ok(T&& value) {
    return result<std::decay_t<T>>{std::forward<T>(value)};
}

/// @brief Construct a successful `result<void>`.
[[nodiscard]] constexpr result<void> ok() {
    return result<void>{};
}

/**
 * @brief Construct an error result.
 * @tparam T Success value type.
 * @param e Error value.
 */
template <class T>
[[nodiscard]] constexpr result<T> err(error e) {
    return std::unexpected<error>{e};
}

} // namespace simplenet
