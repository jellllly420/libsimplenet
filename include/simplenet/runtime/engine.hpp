#pragma once

/**
 * @file
 * @brief Runtime engine that selects and owns one scheduler backend.
 */

#include "simplenet/runtime/event_loop.hpp"
#include "simplenet/runtime/task.hpp"
#include "simplenet/runtime/uring_event_loop.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace simplenet::runtime {

/**
 * @brief Backend-polymorphic event-loop owner.
 */
class engine final {
public:
    /// @brief Supported runtime backend implementations.
    enum class backend {
        epoll,
        io_uring,
    };

    /**
     * @brief Construct engine for a selected backend.
     * @param choice Backend implementation to use.
     * @param uring_queue_depth Ring depth for `io_uring` backend.
     */
    explicit engine(backend choice = backend::epoll,
                    std::uint32_t uring_queue_depth = 256);

    engine(const engine&) = delete;
    engine& operator=(const engine&) = delete;
    engine(engine&&) = delete;
    engine& operator=(engine&&) = delete;

    /// @return Active backend selected at construction.
    [[nodiscard]] backend selected_backend() const noexcept;
    /// @return `true` when backend initialization succeeded.
    [[nodiscard]] bool valid() const noexcept;
    /// @brief Run active backend loop.
    [[nodiscard]] result<void> run() noexcept;
    /// @brief Request active backend loop to stop.
    void stop() noexcept;

    /**
     * @brief Spawn a root task on the active backend.
     * @tparam T Task result type.
     * @param work Task object to transfer.
     */
    template <class T>
    void spawn(task<T>&& work) noexcept {
        if (epoll_loop_.has_value()) {
            epoll_loop_->spawn(std::move(work));
            return;
        }

        if (uring_loop_.has_value()) {
            uring_loop_->spawn(std::move(work));
        }
    }

private:
    backend backend_{backend::epoll};
    std::optional<event_loop> epoll_loop_{};
    std::optional<uring_event_loop> uring_loop_{};
};

} // namespace simplenet::runtime
