#pragma once

/**
 * @file
 * @brief Single-threaded `io_uring`-backed scheduler implementation.
 */

#include "simplenet/core/unique_fd.hpp"
#include "simplenet/runtime/task.hpp"
#include "simplenet/uring/reactor.hpp"

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
 * @brief Coroutine scheduler/event loop backed by `io_uring` poll operations.
 */
class uring_event_loop final : public scheduler {
public:
    /**
     * @brief Construct loop and initialize `io_uring`.
     * @param queue_depth Ring queue depth.
     */
    explicit uring_event_loop(std::uint32_t queue_depth = 256) noexcept;
    /// Destroy loop and outstanding root tasks.
    ~uring_event_loop() override;

    uring_event_loop(const uring_event_loop&) = delete;
    uring_event_loop& operator=(const uring_event_loop&) = delete;
    uring_event_loop(uring_event_loop&&) = delete;
    uring_event_loop& operator=(uring_event_loop&&) = delete;

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
        std::uint64_t token{0};
    };

    struct waiter_slot {
        wait_registration readable{};
        wait_registration writable{};
    };

    struct poll_context {
        int fd{-1};
        bool readable{true};
    };

    [[nodiscard]] result<void>
    arm_waiter(int fd, std::coroutine_handle<> handle, bool readable,
               std::optional<std::chrono::milliseconds> timeout,
               error timeout_error) noexcept;
    [[nodiscard]] result<void> queue_poll_add(std::uint64_t token, int fd,
                                              std::uint32_t poll_mask) noexcept;
    [[nodiscard]] result<void> queue_poll_remove(std::uint64_t token) noexcept;
    [[nodiscard]] result<void> flush_submissions() noexcept;
    void consume_wakeup() noexcept;
    void process_expired_waiters() noexcept;
    void process_completion(const simplenet::uring::completion& completion) noexcept;
    [[nodiscard]] std::uint64_t allocate_token() noexcept;
    [[nodiscard]] static std::uintptr_t
    handle_key(std::coroutine_handle<> handle) noexcept;
    void cleanup_completed_roots() noexcept;
    void destroy_all_roots() noexcept;

    simplenet::uring::reactor reactor_{};
    std::optional<simplenet::error> init_error_{};
    std::optional<simplenet::error> loop_error_{};

    std::deque<std::coroutine_handle<>> ready_queue_{};
    std::unordered_map<int, waiter_slot> waiters_{};
    std::unordered_map<std::uint64_t, poll_context> inflight_polls_{};
    std::unordered_map<std::uintptr_t, result<void>> wait_results_{};
    std::vector<std::coroutine_handle<>> root_tasks_{};

    std::size_t pending_waiter_count_{0};
    std::size_t timed_waiter_count_{0};
    std::size_t active_task_count_{0};
    std::optional<std::chrono::steady_clock::time_point> next_deadline_{};
    bool deadline_index_dirty_{false};
    std::uint64_t next_token_{1};
    std::uint64_t wake_token_{0};
    bool submission_pending_{false};
    std::atomic_bool stop_requested_{false};
    simplenet::unique_fd wake_fd_{};
};

} // namespace simplenet::runtime
