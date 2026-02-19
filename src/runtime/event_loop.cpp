#include "simplenet/runtime/event_loop.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace {

constexpr std::uint32_t kReadReadyMask =
    EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
constexpr std::uint32_t kWriteReadyMask = EPOLLOUT | EPOLLERR | EPOLLHUP;
constexpr std::uint32_t kCommonFlags =
    EPOLLET | EPOLLERR | EPOLLHUP | EPOLLRDHUP;

} // namespace

namespace simplenet::runtime {

event_loop::event_loop() noexcept {
    waiters_.reserve(256);
    wait_results_.reserve(256);
    root_tasks_.reserve(256);

    auto reactor_result = simplenet::epoll::reactor::create();
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

    const auto register_result = reactor_.add(wake_fd_.get(), EPOLLIN);
    if (!register_result.has_value()) {
        init_error_ = register_result.error();
    }
}

event_loop::~event_loop() {
    destroy_all_roots();
}

bool event_loop::valid() const noexcept {
    return !init_error_.has_value() && reactor_.valid();
}

result<void> event_loop::run() noexcept {
    if (!valid()) {
        return err<void>(init_error_.value_or(make_error_from_errno(EINVAL)));
    }

    stop_requested_.store(false, std::memory_order_release);
    loop_error_.reset();

    std::array<simplenet::epoll::ready_event, 64> events{};

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

            int timeout_ms = -1;
            if (next_deadline_.has_value()) {
                const auto now = std::chrono::steady_clock::now();
                const auto remaining = next_deadline_.value() - now;
                if (remaining <= std::chrono::milliseconds{0}) {
                    timeout_ms = 0;
                } else {
                    const auto clamped = std::min<long long>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            remaining)
                            .count(),
                        static_cast<long long>(
                            std::numeric_limits<int>::max()));
                    timeout_ms = static_cast<int>(clamped);
                }
            }

            const auto wait_result =
                reactor_.wait(events, std::chrono::milliseconds{timeout_ms});
            if (!wait_result.has_value()) {
                return err<void>(wait_result.error());
            }

            for (std::size_t i = 0; i < wait_result.value(); ++i) {
                process_ready_event(events[i]);
                if (loop_error_.has_value()) {
                    break;
                }
            }
        }
    }

    cleanup_completed_roots();
    if (loop_error_.has_value()) {
        return err<void>(loop_error_.value());
    }

    return ok();
}

void event_loop::stop() noexcept {
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

void event_loop::schedule(std::coroutine_handle<> handle) noexcept {
    if (!handle) {
        return;
    }
    ready_queue_.push_back(handle);
}

void event_loop::on_task_completed() noexcept {
    if (active_task_count_ > 0) {
        --active_task_count_;
    }
}

result<void>
event_loop::wait_for_readable(int fd, std::coroutine_handle<> handle,
                              std::optional<std::chrono::milliseconds> timeout,
                              error timeout_error) noexcept {
    return arm_waiter(fd, handle, true, timeout, timeout_error);
}

result<void>
event_loop::wait_for_writable(int fd, std::coroutine_handle<> handle,
                              std::optional<std::chrono::milliseconds> timeout,
                              error timeout_error) noexcept {
    return arm_waiter(fd, handle, false, timeout, timeout_error);
}

result<void>
event_loop::consume_wait_result(std::coroutine_handle<> handle) noexcept {
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
event_loop::arm_waiter(int fd, std::coroutine_handle<> handle, bool readable,
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
    auto& slot = it->second;
    auto& target_registration = readable ? slot.readable : slot.writable;
    if (target_registration.handle) {
        return err<void>(make_error_from_errno(EBUSY));
    }

    target_registration.handle = handle;
    target_registration.timeout_error = timeout_error;
    if (timeout.has_value()) {
        target_registration.deadline =
            std::chrono::steady_clock::now() + timeout.value();
        ++timed_waiter_count_;
        if (!next_deadline_.has_value() ||
            target_registration.deadline.value() < next_deadline_.value()) {
            next_deadline_ = target_registration.deadline.value();
        }
    } else {
        target_registration.deadline.reset();
    }
    deadline_index_dirty_ = true;

    ++pending_waiter_count_;
    const auto refresh_result = refresh_interest(fd, slot);
    if (!refresh_result.has_value()) {
        if (target_registration.deadline.has_value() &&
            timed_waiter_count_ > 0) {
            --timed_waiter_count_;
        }
        target_registration = wait_registration{};
        deadline_index_dirty_ = true;
        if (pending_waiter_count_ > 0) {
            --pending_waiter_count_;
        }
        if (!slot.readable.handle && !slot.writable.handle) {
            waiters_.erase(it);
        }
        return refresh_result;
    }

    return ok();
}

result<void> event_loop::refresh_interest(int fd, waiter_slot& slot) noexcept {
    const bool has_read_waiter = static_cast<bool>(slot.readable.handle);
    const bool has_write_waiter = static_cast<bool>(slot.writable.handle);

    std::uint32_t desired_mask = 0;
    if (has_read_waiter || has_write_waiter) {
        desired_mask = kCommonFlags;
        if (has_read_waiter) {
            desired_mask |= EPOLLIN;
        }
        if (has_write_waiter) {
            desired_mask |= EPOLLOUT;
        }
    }

    if (slot.registered_mask == desired_mask) {
        return ok();
    }

    if (slot.registered_mask == 0 && desired_mask != 0) {
        const auto add_result = reactor_.add(fd, desired_mask);
        if (!add_result.has_value()) {
            return add_result;
        }
        slot.registered_mask = desired_mask;
        return ok();
    }

    if (slot.registered_mask != 0 && desired_mask == 0) {
        const auto remove_result = reactor_.remove(fd);
        if (!remove_result.has_value()) {
            return remove_result;
        }
        slot.registered_mask = 0;
        return ok();
    }

    const auto modify_result = reactor_.modify(fd, desired_mask);
    if (!modify_result.has_value()) {
        return modify_result;
    }
    slot.registered_mask = desired_mask;
    return ok();
}

void event_loop::process_expired_waiters() noexcept {
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
        bool changed = false;

        if (it->second.readable.handle &&
            it->second.readable.deadline.has_value() &&
            now >= it->second.readable.deadline.value()) {
            wait_results_[handle_key(it->second.readable.handle)] =
                err<void>(it->second.readable.timeout_error);
            schedule(it->second.readable.handle);
            if (timed_waiter_count_ > 0) {
                --timed_waiter_count_;
            }
            it->second.readable = wait_registration{};
            if (pending_waiter_count_ > 0) {
                --pending_waiter_count_;
            }
            changed = true;
        }

        if (it->second.writable.handle &&
            it->second.writable.deadline.has_value() &&
            now >= it->second.writable.deadline.value()) {
            wait_results_[handle_key(it->second.writable.handle)] =
                err<void>(it->second.writable.timeout_error);
            schedule(it->second.writable.handle);
            if (timed_waiter_count_ > 0) {
                --timed_waiter_count_;
            }
            it->second.writable = wait_registration{};
            if (pending_waiter_count_ > 0) {
                --pending_waiter_count_;
            }
            changed = true;
        }

        if (changed) {
            const auto refresh_result = refresh_interest(it->first, it->second);
            if (!refresh_result.has_value()) {
                loop_error_ = refresh_result.error();
                stop_requested_.store(true, std::memory_order_release);
                return;
            }
        }

        if (!it->second.readable.handle && !it->second.writable.handle) {
            it = waiters_.erase(it);
        } else {
            if (it->second.readable.handle &&
                it->second.readable.deadline.has_value()) {
                next_deadline =
                    std::min(next_deadline, it->second.readable.deadline.value());
                has_next_deadline = true;
            }
            if (it->second.writable.handle &&
                it->second.writable.deadline.has_value()) {
                next_deadline =
                    std::min(next_deadline, it->second.writable.deadline.value());
                has_next_deadline = true;
            }
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

void event_loop::consume_wakeup() noexcept {
    if (!wake_fd_.valid()) {
        return;
    }

    std::uint64_t signal = 0;
    while (::read(wake_fd_.get(), &signal, sizeof(signal)) < 0 && errno == EINTR) {}
}

void event_loop::process_ready_event(
    const simplenet::epoll::ready_event& event) noexcept {
    if (wake_fd_.valid() && event.fd == wake_fd_.get()) {
        consume_wakeup();
        return;
    }

    auto it = waiters_.find(event.fd);
    if (it == waiters_.end()) {
        return;
    }

    auto& slot = it->second;

    if (slot.readable.handle &&
        simplenet::epoll::has_event(event.events, kReadReadyMask)) {
        wait_results_[handle_key(slot.readable.handle)] = ok();
        schedule(slot.readable.handle);
        if (slot.readable.deadline.has_value() && timed_waiter_count_ > 0) {
            --timed_waiter_count_;
            deadline_index_dirty_ = true;
        }
        slot.readable = wait_registration{};
        if (pending_waiter_count_ > 0) {
            --pending_waiter_count_;
        }
    }

    if (slot.writable.handle &&
        simplenet::epoll::has_event(event.events, kWriteReadyMask)) {
        wait_results_[handle_key(slot.writable.handle)] = ok();
        schedule(slot.writable.handle);
        if (slot.writable.deadline.has_value() && timed_waiter_count_ > 0) {
            --timed_waiter_count_;
            deadline_index_dirty_ = true;
        }
        slot.writable = wait_registration{};
        if (pending_waiter_count_ > 0) {
            --pending_waiter_count_;
        }
    }

    const auto refresh_result = refresh_interest(event.fd, slot);
    if (!refresh_result.has_value()) {
        loop_error_ = refresh_result.error();
        stop_requested_.store(true, std::memory_order_release);
        return;
    }

    if (!slot.readable.handle && !slot.writable.handle) {
        waiters_.erase(it);
    }
}

std::uintptr_t event_loop::handle_key(std::coroutine_handle<> handle) noexcept {
    return reinterpret_cast<std::uintptr_t>(handle.address());
}

void event_loop::cleanup_completed_roots() noexcept {
    for (auto it = root_tasks_.begin(); it != root_tasks_.end(); ) {
        if (it->done()) {
            it->destroy();
            it = root_tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

void event_loop::destroy_all_roots() noexcept {
    for (auto handle : root_tasks_) {
        if (handle) {
            handle.destroy();
        }
    }
    root_tasks_.clear();
}

} // namespace simplenet::runtime
