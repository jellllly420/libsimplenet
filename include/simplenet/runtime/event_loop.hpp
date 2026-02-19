#pragma once

/**
 * @file
 * @brief Single-threaded epoll-backed scheduler implementation.
 */

#include "simplenet/epoll/reactor.hpp"
#include "simplenet/runtime/task.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace simplenet::runtime {

/**
 * @brief Coroutine scheduler/event loop backed by `epoll`.
 */
class event_loop final : public scheduler {
public:
    /// Construct and initialize loop resources.
    event_loop() noexcept;
    /// Destroy loop and outstanding root tasks.
    ~event_loop() override;

    event_loop(const event_loop&) = delete;
    event_loop& operator=(const event_loop&) = delete;
    event_loop(event_loop&&) = delete;
    event_loop& operator=(event_loop&&) = delete;

    /// @return `true` when initialization succeeded.
    [[nodiscard]] bool valid() const noexcept;
    /// @brief Run loop until all root tasks complete or `stop()` is requested.
    [[nodiscard]] result<void> run() noexcept;
    /// @brief Request loop shutdown.
    void stop() noexcept;

    /**
     * @brief Spawn a root task tracked by this loop.
     * @tparam T Task result type.
     * @param work Task object to transfer.
     */
    template <class T>
    void spawn(task<T>&& work) noexcept {
        auto handle = work.release();
        if (!handle) {
            return;
        }

        handle.promise().set_scheduler(this, true);
        ++active_task_count_;
        root_tasks_.push_back(handle);
        schedule(handle);
    }

    /// @brief Queue a coroutine for resume on the loop thread.
    void schedule(std::coroutine_handle<> handle) noexcept override;
    /// @brief Notify loop that a tracked root task has finished.
    void on_task_completed() noexcept override;
    /// @brief Suspend coroutine until descriptor is readable.
    [[nodiscard]] result<void>
    wait_for_readable(int fd, std::coroutine_handle<> handle,
                      std::optional<std::chrono::milliseconds> timeout,
                      error timeout_error) noexcept override;
    /// @brief Suspend coroutine until descriptor is writable.
    [[nodiscard]] result<void>
    wait_for_writable(int fd, std::coroutine_handle<> handle,
                      std::optional<std::chrono::milliseconds> timeout,
                      error timeout_error) noexcept override;
    /// @brief Consume readiness/timeout result produced for a waiting coroutine.
    [[nodiscard]] result<void>
    consume_wait_result(std::coroutine_handle<> handle) noexcept override;

private:
    struct wait_registration {
        std::coroutine_handle<> handle{};
        std::optional<std::chrono::steady_clock::time_point> deadline{};
        error timeout_error = make_error_from_errno(ETIMEDOUT);
    };

    struct waiter_slot {
        wait_registration readable{};
        wait_registration writable{};
        std::uint32_t registered_mask{0};
    };

    [[nodiscard]] result<void>
    arm_waiter(int fd, std::coroutine_handle<> handle, bool readable,
               std::optional<std::chrono::milliseconds> timeout,
               error timeout_error) noexcept;
    [[nodiscard]] result<void> refresh_interest(int fd,
                                                waiter_slot& slot) noexcept;
    void process_expired_waiters() noexcept;
    void consume_wakeup() noexcept;
    void process_ready_event(const simplenet::epoll::ready_event& event) noexcept;
    [[nodiscard]] static std::uintptr_t
    handle_key(std::coroutine_handle<> handle) noexcept;
    void cleanup_completed_roots() noexcept;
    void destroy_all_roots() noexcept;

    simplenet::epoll::reactor reactor_{};
    std::optional<simplenet::error> init_error_{};
    std::optional<simplenet::error> loop_error_{};

    std::deque<std::coroutine_handle<>> ready_queue_{};
    std::unordered_map<int, waiter_slot> waiters_{};
    std::unordered_map<std::uintptr_t, result<void>> wait_results_{};
    std::vector<std::coroutine_handle<>> root_tasks_{};

    std::size_t pending_waiter_count_{0};
    std::size_t timed_waiter_count_{0};
    std::size_t active_task_count_{0};
    std::optional<std::chrono::steady_clock::time_point> next_deadline_{};
    bool deadline_index_dirty_{false};
    std::atomic_bool stop_requested_{false};
    simplenet::unique_fd wake_fd_{};
};

} // namespace simplenet::runtime
