#include "simplenet/runtime/uring_event_loop.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <limits>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace {

constexpr std::uint32_t kReadPollMask =
    static_cast<std::uint32_t>(POLLIN | POLLERR | POLLHUP | POLLRDHUP);
constexpr std::uint32_t kWritePollMask =
    static_cast<std::uint32_t>(POLLOUT | POLLERR | POLLHUP);

} // namespace

namespace simplenet::runtime {

uring_event_loop::uring_event_loop(std::uint32_t queue_depth) noexcept {
    waiters_.reserve(static_cast<std::size_t>(queue_depth));
    inflight_polls_.reserve(static_cast<std::size_t>(queue_depth) * 2U);
    wait_results_.reserve(static_cast<std::size_t>(queue_depth) * 2U);
    root_tasks_.reserve(256);

    auto reactor_result = simplenet::uring::reactor::create(queue_depth);
    if (!reactor_result.has_value()) {
        init_error_ = reactor_result.error();
        return;
    }
    reactor_ = std::move(reactor_result.value());

    const int wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd < 0) {
        init_error_ = error::from_errno();
        return;
    }
    wake_fd_ = simplenet::unique_fd{wake_fd};

    wake_token_ = allocate_token();
    const auto wake_add =
        queue_poll_add(wake_token_, wake_fd_.get(), kReadPollMask);
    if (!wake_add.has_value()) {
        init_error_ = wake_add.error();
        return;
    }

    const auto flush_result = flush_submissions();
    if (!flush_result.has_value()) {
        init_error_ = flush_result.error();
    }
}

uring_event_loop::~uring_event_loop() {
    destroy_all_roots();
}

bool uring_event_loop::valid() const noexcept {
    return init_error_.has_value() == false && reactor_.valid();
}

result<void> uring_event_loop::run() noexcept {
    if (!valid()) {
        return err<void>(init_error_.value_or(make_error_from_errno(EINVAL)));
    }

    stop_requested_.store(false, std::memory_order_release);
    loop_error_.reset();

    std::array<simplenet::uring::completion, 64> completions{};

    while (!stop_requested_.load(std::memory_order_acquire)) {
        process_expired_waiters();
        if (stop_requested_.load(std::memory_order_acquire) ||
            loop_error_.has_value()) {
            break;
        }

        while (!ready_queue_.empty()) {
            const auto handle = ready_queue_.front();
            ready_queue_.pop_front();

            if (!handle || handle.done()) {
                continue;
            }

            handle.resume();
            cleanup_completed_roots();
            process_expired_waiters();

            if (stop_requested_.load(std::memory_order_acquire) ||
                loop_error_.has_value()) {
                break;
            }
        }

        if (stop_requested_.load(std::memory_order_acquire) ||
            loop_error_.has_value()) {
            break;
        }

        if (ready_queue_.empty()) {
            if (active_task_count_ == 0 && pending_waiter_count_ == 0) {
                break;
            }

            if (pending_waiter_count_ == 0) {
                return err<void>(make_error_from_errno(EDEADLK));
            }

            auto wait_timeout = std::optional<std::chrono::milliseconds>{};
            if (next_deadline_.has_value()) {
                const auto now = std::chrono::steady_clock::now();
                const auto remaining = next_deadline_.value() - now;
                if (remaining <= std::chrono::milliseconds{0}) {
                    wait_timeout = std::chrono::milliseconds{0};
                } else {
                    const auto clamped = std::min<long long>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            remaining)
                            .count(),
                        static_cast<long long>(
                            std::numeric_limits<int>::max()));
                    wait_timeout = std::chrono::milliseconds{clamped};
                }
            }

            const auto submit_result = flush_submissions();
            if (!submit_result.has_value()) {
                return submit_result;
            }

            const auto wait_result = reactor_.wait(completions, wait_timeout);
            if (!wait_result.has_value()) {
                return err<void>(wait_result.error());
            }

            for (std::size_t i = 0; i < wait_result.value(); ++i) {
                process_completion(completions[i]);
                if (loop_error_.has_value()) {
                    break;
                }
            }
        }
    }

    cleanup_completed_roots();
    const auto flush_result = flush_submissions();
    if (!flush_result.has_value() && !loop_error_.has_value()) {
        loop_error_ = flush_result.error();
    }

    if (loop_error_.has_value()) {
        return err<void>(loop_error_.value());
    }

    return ok();
}

void uring_event_loop::stop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
    if (!wake_fd_.valid()) {
        return;
    }

    std::uint64_t signal = 1;
    while (::write(wake_fd_.get(), &signal, sizeof(signal)) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        break;
    }
}

void uring_event_loop::schedule(std::coroutine_handle<> handle) noexcept {
    if (!handle) {
        return;
    }
    ready_queue_.push_back(handle);
}

void uring_event_loop::on_task_completed() noexcept {
    if (active_task_count_ > 0) {
        --active_task_count_;
    }
}

result<void> uring_event_loop::wait_for_readable(
    int fd, std::coroutine_handle<> handle,
    std::optional<std::chrono::milliseconds> timeout,
    error timeout_error) noexcept {
    return arm_waiter(fd, handle, true, timeout, timeout_error);
}

result<void> uring_event_loop::wait_for_writable(
    int fd, std::coroutine_handle<> handle,
    std::optional<std::chrono::milliseconds> timeout,
    error timeout_error) noexcept {
    return arm_waiter(fd, handle, false, timeout, timeout_error);
}

result<void>
uring_event_loop::consume_wait_result(std::coroutine_handle<> handle) noexcept {
    if (!handle) {
        return err<void>(make_error_from_errno(EINVAL));
    }

    const auto key = handle_key(handle);
    auto it = wait_results_.find(key);
    if (it == wait_results_.end()) {
        return ok();
    }

    auto status = std::move(it->second);
    wait_results_.erase(it);
    return status;
}

result<void>
uring_event_loop::arm_waiter(int fd, std::coroutine_handle<> handle,
                             bool readable,
                             std::optional<std::chrono::milliseconds> timeout,
                             error timeout_error) noexcept {
    if (!valid()) {
        return err<void>(init_error_.value_or(make_error_from_errno(EINVAL)));
    }
    if (fd < 0 || !handle) {
        return err<void>(make_error_from_errno(EBADF));
    }

    if (timeout.has_value() &&
        timeout.value() <= std::chrono::milliseconds{0}) {
        wait_results_[handle_key(handle)] = err<void>(timeout_error);
        schedule(handle);
        return ok();
    }

    auto [it, inserted] = waiters_.try_emplace(fd);
    (void)inserted;
    auto& slot = it->second;
    auto& target = readable ? slot.readable : slot.writable;
    if (target.handle) {
        return err<void>(make_error_from_errno(EBUSY));
    }

    const auto token = allocate_token();
    target.handle = handle;
    target.timeout_error = timeout_error;
    target.token = token;
    if (timeout.has_value()) {
        target.deadline = std::chrono::steady_clock::now() + timeout.value();
        ++timed_waiter_count_;
        if (!next_deadline_.has_value() ||
            target.deadline.value() < next_deadline_.value()) {
            next_deadline_ = target.deadline.value();
        }
    } else {
        target.deadline.reset();
    }
    deadline_index_dirty_ = true;

    ++pending_waiter_count_;
    inflight_polls_[token] = poll_context{fd, readable};

    const auto add_result =
        queue_poll_add(token, fd, readable ? kReadPollMask : kWritePollMask);
    if (!add_result.has_value()) {
        inflight_polls_.erase(token);
        if (target.deadline.has_value() && timed_waiter_count_ > 0) {
            --timed_waiter_count_;
        }
        target = wait_registration{};
        deadline_index_dirty_ = true;
        if (pending_waiter_count_ > 0) {
            --pending_waiter_count_;
        }
        if (!slot.readable.handle && !slot.writable.handle) {
            waiters_.erase(it);
        }
        return add_result;
    }

    return ok();
}

result<void>
uring_event_loop::queue_poll_add(std::uint64_t token, int fd,
                                 std::uint32_t poll_mask) noexcept {
    auto add_result = reactor_.submit_poll_add(token, fd, poll_mask);
    if (!add_result.has_value() && add_result.error().value() == EBUSY) {
        const auto flush_result = flush_submissions();
        if (!flush_result.has_value()) {
            return flush_result;
        }
        add_result = reactor_.submit_poll_add(token, fd, poll_mask);
    }

    if (!add_result.has_value()) {
        return add_result;
    }

    submission_pending_ = true;
    return ok();
}

result<void> uring_event_loop::queue_poll_remove(std::uint64_t token) noexcept {
    if (token == 0U) {
        return ok();
    }

    auto remove_result = reactor_.submit_poll_remove(token);
    if (!remove_result.has_value() && remove_result.error().value() == EBUSY) {
        const auto flush_result = flush_submissions();
        if (!flush_result.has_value()) {
            return flush_result;
        }
        remove_result = reactor_.submit_poll_remove(token);
    }

    if (!remove_result.has_value()) {
        if (remove_result.error().value() == ENOENT) {
            return ok();
        }
        return remove_result;
    }

    submission_pending_ = true;
    return ok();
}

result<void> uring_event_loop::flush_submissions() noexcept {
    if (!submission_pending_) {
        return ok();
    }

    const auto submit_result = reactor_.submit();
    if (!submit_result.has_value()) {
        return submit_result;
    }

    submission_pending_ = false;
    return ok();
}

void uring_event_loop::process_expired_waiters() noexcept {
    if (timed_waiter_count_ == 0) {
        next_deadline_.reset();
        deadline_index_dirty_ = false;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!deadline_index_dirty_ && next_deadline_.has_value() &&
        now < next_deadline_.value()) {
        return;
    }

    auto next_deadline = std::chrono::steady_clock::time_point::max();
    bool has_next_deadline = false;

    for (auto it = waiters_.begin(); it != waiters_.end();) {
        auto expire_registration = [&](wait_registration& registration) {
            if (!registration.handle || !registration.deadline.has_value() ||
                now < registration.deadline.value()) {
                if (registration.handle && registration.deadline.has_value()) {
                    next_deadline =
                        std::min(next_deadline, registration.deadline.value());
                    has_next_deadline = true;
                }
                return;
            }

            wait_results_[handle_key(registration.handle)] =
                err<void>(registration.timeout_error);
            schedule(registration.handle);

            const auto token = registration.token;
            if (timed_waiter_count_ > 0) {
                --timed_waiter_count_;
            }
            registration = wait_registration{};

            if (pending_waiter_count_ > 0) {
                --pending_waiter_count_;
            }

            if (token != 0U) {
                inflight_polls_.erase(token);
                const auto remove_result = queue_poll_remove(token);
                if (!remove_result.has_value()) {
                    loop_error_ = remove_result.error();
                    stop_requested_.store(true, std::memory_order_release);
                }
            }
        };

        expire_registration(it->second.readable);
        if (loop_error_.has_value()) {
            return;
        }

        expire_registration(it->second.writable);
        if (loop_error_.has_value()) {
            return;
        }

        if (!it->second.readable.handle && !it->second.writable.handle) {
            it = waiters_.erase(it);
        } else {
            ++it;
        }
    }

    if (has_next_deadline) {
        next_deadline_ = next_deadline;
    } else {
        next_deadline_.reset();
    }
    deadline_index_dirty_ = false;
}

void uring_event_loop::consume_wakeup() noexcept {
    if (!wake_fd_.valid()) {
        return;
    }

    std::uint64_t signal = 0;
    while (true) {
        const auto count = ::read(wake_fd_.get(), &signal, sizeof(signal));
        if (count > 0) {
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

void uring_event_loop::process_completion(
    const simplenet::uring::completion& completion) noexcept {
    const auto token = completion.user_data;
    if (token == 0U) {
        return;
    }

    if (token == wake_token_) {
        consume_wakeup();

        if (!stop_requested_.load(std::memory_order_acquire)) {
            const auto rearm =
                queue_poll_add(wake_token_, wake_fd_.get(), kReadPollMask);
            if (!rearm.has_value()) {
                loop_error_ = rearm.error();
                stop_requested_.store(true, std::memory_order_release);
                return;
            }

            const auto flush_result = flush_submissions();
            if (!flush_result.has_value()) {
                loop_error_ = flush_result.error();
                stop_requested_.store(true, std::memory_order_release);
            }
        }
        return;
    }

    auto inflight_it = inflight_polls_.find(token);
    if (inflight_it == inflight_polls_.end()) {
        return;
    }

    const auto context = inflight_it->second;
    inflight_polls_.erase(inflight_it);

    auto waiter_it = waiters_.find(context.fd);
    if (waiter_it == waiters_.end()) {
        return;
    }

    auto& slot = waiter_it->second;
    auto& registration = context.readable ? slot.readable : slot.writable;

    if (!registration.handle || registration.token != token) {
        return;
    }

    if (completion.result >= 0) {
        wait_results_[handle_key(registration.handle)] = ok();
    } else {
        wait_results_[handle_key(registration.handle)] =
            err<void>(make_error_from_errno(-completion.result));
    }

    schedule(registration.handle);
    if (registration.deadline.has_value() && timed_waiter_count_ > 0) {
        --timed_waiter_count_;
        deadline_index_dirty_ = true;
    }
    registration = wait_registration{};

    if (pending_waiter_count_ > 0) {
        --pending_waiter_count_;
    }

    if (!slot.readable.handle && !slot.writable.handle) {
        waiters_.erase(waiter_it);
    }
}

std::uint64_t uring_event_loop::allocate_token() noexcept {
    while (true) {
        auto token = next_token_;
        ++next_token_;

        if (next_token_ == 0U) {
            ++next_token_;
        }

        if (token == 0U) {
            continue;
        }

        if (!inflight_polls_.contains(token)) {
            return token;
        }
    }
}

std::uintptr_t
uring_event_loop::handle_key(std::coroutine_handle<> handle) noexcept {
    return reinterpret_cast<std::uintptr_t>(handle.address());
}

void uring_event_loop::cleanup_completed_roots() noexcept {
    const auto new_end = std::remove_if(root_tasks_.begin(), root_tasks_.end(),
                                        [](std::coroutine_handle<> handle) {
                                            if (!handle.done()) {
                                                return false;
                                            }
                                            handle.destroy();
                                            return true;
                                        });
    root_tasks_.erase(new_end, root_tasks_.end());
}

void uring_event_loop::destroy_all_roots() noexcept {
    for (auto handle : root_tasks_) {
        if (handle) {
            handle.destroy();
        }
    }

    root_tasks_.clear();
    waiters_.clear();
    inflight_polls_.clear();
    wait_results_.clear();
    pending_waiter_count_ = 0;
    active_task_count_ = 0;
}

} // namespace simplenet::runtime
