#pragma once

/**
 * @file
 * @brief High-level runtime context used to drive async tasks.
 */

#include "simplenet/runtime/engine.hpp"

#include <cstdint>
#include <utility>

namespace simplenet {

/**
 * @brief Owns and runs a single async runtime engine instance.
 *
 * `io_context` is a convenience wrapper over `runtime::engine` that mirrors
 * the usage model of common networking libraries: spawn tasks, run the loop,
 * and stop it.
 */
class io_context {
public:
    /// Runtime backend selector.
    using backend = runtime::engine::backend;

    /**
     * @brief Construct a runtime context.
     * @param selected_backend Backend implementation to use.
     * @param uring_queue_depth SQ/CQ entry count when `io_uring` is selected.
     */
    explicit io_context(backend selected_backend = backend::epoll,
                        std::uint32_t uring_queue_depth = 256)
        : engine_(selected_backend, uring_queue_depth) {}

    /// @return `true` when backend initialization succeeded.
    [[nodiscard]] bool valid() const noexcept {
        return engine_.valid();
    }

    /// @return The backend selected during construction.
    [[nodiscard]] backend selected_backend() const noexcept {
        return engine_.selected_backend();
    }

    /**
     * @brief Schedule a root coroutine task.
     * @tparam T Task result type.
     * @param work Task object to transfer into the runtime.
     */
    template <class T>
    void spawn(runtime::task<T>&& work) noexcept {
        engine_.spawn(std::move(work));
    }

    /**
     * @brief Run the event loop until all root tasks complete or stop is requested.
     * @return Success or the first loop error.
     */
    [[nodiscard]] result<void> run() noexcept {
        return engine_.run();
    }

    /// @brief Request loop shutdown at the next wake-up boundary.
    void stop() noexcept {
        engine_.stop();
    }

private:
    runtime::engine engine_;
};

} // namespace simplenet
