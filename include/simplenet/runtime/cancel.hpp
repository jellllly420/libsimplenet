#pragma once

/**
 * @file
 * @brief Cooperative cancellation primitives for async operations.
 */

#include <atomic>
#include <memory>

namespace simplenet::runtime {

/**
 * @brief Read-only cancellation token shared with async operations.
 */
class cancel_token {
public:
    /// Construct a token that never cancels.
    cancel_token() = default;

    /// @return `true` when associated source has requested cancellation.
    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ != nullptr && state_->load(std::memory_order_acquire);
    }

private:
    friend class cancel_source;
    explicit cancel_token(std::shared_ptr<std::atomic<bool>> state)
        : state_(std::move(state)) {}

    std::shared_ptr<std::atomic<bool>> state_{};
};

/**
 * @brief Cancellation source that can signal one or more tokens.
 */
class cancel_source {
public:
    /// Construct an active source.
    cancel_source() : state_(std::make_shared<std::atomic<bool>>(false)) {}

    /// @return Token bound to this source.
    [[nodiscard]] cancel_token token() const {
        return cancel_token{state_};
    }

    /// @brief Request cancellation for all tokens derived from this source.
    void request_stop() const noexcept {
        state_->store(true, std::memory_order_release);
    }

private:
    std::shared_ptr<std::atomic<bool>> state_{};
};

} // namespace simplenet::runtime
