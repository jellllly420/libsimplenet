#pragma once

/**
 * @file
 * @brief Coroutine task type and scheduler interface used by the async runtime.
 */

#include "simplenet/core/result.hpp"

#include <chrono>
#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace simplenet::runtime {

/**
 * @brief Scheduling interface implemented by runtime event loops.
 */
class scheduler {
public:
    virtual ~scheduler() = default;

    /// @brief Queue a coroutine for execution/resume.
    virtual void schedule(std::coroutine_handle<> handle) noexcept = 0;
    /// @brief Notify scheduler when a tracked root task reaches final suspend.
    virtual void on_task_completed() noexcept = 0;
    /**
     * @brief Register wait-until-readable interest for a descriptor.
     * @param fd Descriptor to monitor.
     * @param handle Waiting coroutine.
     * @param timeout Optional timeout.
     * @param timeout_error Error returned if timeout expires.
     */
    virtual result<void>
    wait_for_readable(int fd, std::coroutine_handle<> handle,
                      std::optional<std::chrono::milliseconds> timeout,
                      error timeout_error) noexcept = 0;
    /**
     * @brief Register wait-until-writable interest for a descriptor.
     * @param fd Descriptor to monitor.
     * @param handle Waiting coroutine.
     * @param timeout Optional timeout.
     * @param timeout_error Error returned if timeout expires.
     */
    virtual result<void>
    wait_for_writable(int fd, std::coroutine_handle<> handle,
                      std::optional<std::chrono::milliseconds> timeout,
                      error timeout_error) noexcept = 0;
    /**
     * @brief Retrieve wake-up outcome for a waiter coroutine.
     * @param handle Coroutine handle used as waiter key.
     */
    virtual result<void>
    consume_wait_result(std::coroutine_handle<> handle) noexcept = 0;
};

namespace detail {

class task_promise_base {
public:
    task_promise_base() noexcept = default;
    ~task_promise_base() = default;
    task_promise_base(const task_promise_base&) = default;
    task_promise_base& operator=(const task_promise_base&) = default;

    [[nodiscard]] scheduler *scheduler_ptr() const noexcept {
        return scheduler_;
    }

    void set_scheduler(scheduler *value, bool tracked) noexcept {
        scheduler_ = value;
        tracked_ = tracked;
    }

    void set_continuation(std::coroutine_handle<> continuation) noexcept {
        continuation_ = continuation;
    }

    [[nodiscard]] std::coroutine_handle<> continuation() const noexcept {
        return continuation_;
    }

    [[nodiscard]] bool tracked() const noexcept {
        return tracked_;
    }

private:
    scheduler *scheduler_{nullptr};
    std::coroutine_handle<> continuation_{};
    bool tracked_{false};
};

struct task_final_awaiter {
    [[nodiscard]] bool await_ready() const noexcept {
        return false;
    }

    template <class Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto& promise = handle.promise();
        auto *scheduler = promise.scheduler_ptr();

        if (promise.tracked() && scheduler != nullptr) {
            scheduler->on_task_completed();
        }

        const auto continuation = promise.continuation();
        if (!continuation) {
            return;
        }

        if (scheduler != nullptr) {
            scheduler->schedule(continuation);
            return;
        }

        continuation.resume();
    }

    void await_resume() const noexcept {}
};

} // namespace detail

template <class T>
class task {
public:
    /// Promise type backing `task<T>`.
    struct promise_type : detail::task_promise_base {
        [[nodiscard]] task get_return_object() noexcept {
            return task{handle_type::from_promise(*this)};
        }

        [[nodiscard]] std::suspend_always initial_suspend() const noexcept {
            return {};
        }

        [[nodiscard]] detail::task_final_awaiter
        final_suspend() const noexcept {
            return {};
        }

        /// @brief Return value from coroutine body.
        template <class U>
            requires std::convertible_to<U, T>
        void return_value(U&& value) noexcept(
            std::is_nothrow_constructible_v<T, U&&>) {
            value_.emplace(std::forward<U>(value));
        }

        /// @brief Store the active exception for later rethrow.
        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }

        /// @brief Consume and move the coroutine result.
        [[nodiscard]] T consume_result() {
            if (exception_ != nullptr) {
                std::rethrow_exception(exception_);
            }
            if (!value_.has_value()) {
                throw std::logic_error("task result is not available");
            }
            return std::move(*value_);
        }

    private:
        std::optional<T> value_{};
        std::exception_ptr exception_{};
    };

    /// Coroutine handle type for this task.
    using handle_type = std::coroutine_handle<promise_type>;

    /// Construct an empty task.
    task() noexcept = default;
    /// Construct from a coroutine handle.
    explicit task(handle_type handle) noexcept : handle_(handle) {}

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    /// Move-construct ownership of coroutine state.
    task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    /// Move-assign ownership of coroutine state.
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    /// Destroy coroutine state if still owned.
    ~task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    /// @return `true` when a coroutine handle is owned.
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<bool>(handle_);
    }

    /// @return `true` when task has completed or is empty.
    [[nodiscard]] bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    /**
     * @brief Release the coroutine handle to caller ownership.
     * @return Owned handle, leaving this task empty.
     */
    [[nodiscard]] handle_type release() noexcept {
        return std::exchange(handle_, {});
    }

    /**
     * @brief Awaiter that transfers/resumes child task execution.
     */
    struct awaiter {
        handle_type handle_{};

        [[nodiscard]] bool await_ready() const noexcept {
            return !handle_ || handle_.done();
        }

        /// @brief Wire continuation and schedule/resume child coroutine.
        template <class Promise>
        bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept {
            auto& child = handle_.promise();
            child.set_continuation(awaiting);

            if constexpr (requires(Promise& p) { p.scheduler_ptr(); }) {
                if (child.scheduler_ptr() == nullptr) {
                    child.set_scheduler(awaiting.promise().scheduler_ptr(),
                                        false);
                }
            }

            auto *scheduler = child.scheduler_ptr();
            if (scheduler != nullptr) {
                scheduler->schedule(handle_);
                return true;
            }

            handle_.resume();
            return false;
        }

        /// @brief Return child result or rethrow child exception.
        T await_resume() {
            if (!handle_) {
                throw std::logic_error("awaited task has no coroutine handle");
            }

            auto value = handle_.promise().consume_result();
            handle_.destroy();
            handle_ = {};
            return value;
        }

        ~awaiter() {
            if (handle_) {
                handle_.destroy();
            }
        }
    };

    /// @brief Await this task, transferring ownership to awaiter.
    [[nodiscard]] awaiter operator co_await() && noexcept {
        return awaiter{std::exchange(handle_, {})};
    }

private:
    handle_type handle_{};
};

template <>
class task<void> {
public:
    /// Promise type backing `task<void>`.
    struct promise_type : detail::task_promise_base {
        [[nodiscard]] task get_return_object() noexcept {
            return task{handle_type::from_promise(*this)};
        }

        [[nodiscard]] std::suspend_always initial_suspend() const noexcept {
            return {};
        }

        [[nodiscard]] detail::task_final_awaiter
        final_suspend() const noexcept {
            return {};
        }

        /// @brief Complete coroutine with no value.
        void return_void() const noexcept {}

        /// @brief Store the active exception for later rethrow.
        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }

        /// @brief Rethrow exception, if any.
        void consume_result() {
            if (exception_ != nullptr) {
                std::rethrow_exception(exception_);
            }
        }

    private:
        std::exception_ptr exception_{};
    };

    /// Coroutine handle type for this task specialization.
    using handle_type = std::coroutine_handle<promise_type>;

    /// Construct an empty task.
    task() noexcept = default;
    /// Construct from a coroutine handle.
    explicit task(handle_type handle) noexcept : handle_(handle) {}

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    /// Move-construct ownership of coroutine state.
    task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    /// Move-assign ownership of coroutine state.
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    /// Destroy coroutine state if still owned.
    ~task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    /// @return `true` when a coroutine handle is owned.
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<bool>(handle_);
    }

    /// @return `true` when task has completed or is empty.
    [[nodiscard]] bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    /**
     * @brief Release the coroutine handle to caller ownership.
     * @return Owned handle, leaving this task empty.
     */
    [[nodiscard]] handle_type release() noexcept {
        return std::exchange(handle_, {});
    }

    /**
     * @brief Awaiter that transfers/resumes child task execution.
     */
    struct awaiter {
        handle_type handle_{};

        [[nodiscard]] bool await_ready() const noexcept {
            return !handle_ || handle_.done();
        }

        /// @brief Wire continuation and schedule/resume child coroutine.
        template <class Promise>
        bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept {
            auto& child = handle_.promise();
            child.set_continuation(awaiting);

            if constexpr (requires(Promise& p) { p.scheduler_ptr(); }) {
                if (child.scheduler_ptr() == nullptr) {
                    child.set_scheduler(awaiting.promise().scheduler_ptr(),
                                        false);
                }
            }

            auto *scheduler = child.scheduler_ptr();
            if (scheduler != nullptr) {
                scheduler->schedule(handle_);
                return true;
            }

            handle_.resume();
            return false;
        }

        /// @brief Rethrow child exception, if any.
        void await_resume() {
            if (!handle_) {
                throw std::logic_error("awaited task has no coroutine handle");
            }

            handle_.promise().consume_result();
            handle_.destroy();
            handle_ = {};
        }

        ~awaiter() {
            if (handle_) {
                handle_.destroy();
            }
        }
    };

    /// @brief Await this task, transferring ownership to awaiter.
    [[nodiscard]] awaiter operator co_await() && noexcept {
        return awaiter{std::exchange(handle_, {})};
    }

private:
    handle_type handle_{};
};

} // namespace simplenet::runtime
