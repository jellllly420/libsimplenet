#include "simplenet/epoll/reactor.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <sys/epoll.h>
#include <vector>

namespace simplenet::epoll {

namespace {

constexpr std::size_t kMaxCachedEventBatch = 1024;

} // namespace

reactor::reactor(simplenet::unique_fd epoll_fd) noexcept
    : epoll_fd_(std::move(epoll_fd)) {}

result<reactor> reactor::create() noexcept {
    const int fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
        return err<reactor>(error::from_errno());
    }

    return reactor{simplenet::unique_fd{fd}};
}

result<void> reactor::add(int fd, std::uint32_t events) noexcept {
    return ctl(EPOLL_CTL_ADD, fd, events, true);
}

result<void> reactor::modify(int fd, std::uint32_t events) noexcept {
    return ctl(EPOLL_CTL_MOD, fd, events, true);
}

result<void> reactor::remove(int fd) noexcept {
    return ctl(EPOLL_CTL_DEL, fd, 0, false);
}

result<std::size_t> reactor::wait(std::span<ready_event> events,
                                  std::chrono::milliseconds timeout) noexcept {
    if (!valid()) {
        return err<std::size_t>(make_error_from_errno(EBADF));
    }
    if (events.empty()) {
        return err<std::size_t>(make_error_from_errno(EINVAL));
    }

    ::epoll_event *sys_events_ptr = nullptr;
    std::vector<::epoll_event> uncached_events{};

    if (events.size() <= kMaxCachedEventBatch) {
        thread_local std::vector<::epoll_event> cached_events{};
        if (cached_events.size() < events.size()) {
            cached_events.resize(events.size());
        }
        sys_events_ptr = cached_events.data();
    } else {
        uncached_events.resize(events.size());
        sys_events_ptr = uncached_events.data();
    }

    int timeout_ms = -1;
    if (timeout.count() >= 0) {
        const auto clamped = std::min<long long>(
            timeout.count(),
            static_cast<long long>(std::numeric_limits<int>::max()));
        timeout_ms = static_cast<int>(clamped);
    }

    const int ready_count =
        ::epoll_wait(epoll_fd_.get(), sys_events_ptr,
                     static_cast<int>(events.size()), timeout_ms);
    if (ready_count < 0) {
        if (errno == EINTR) {
            return static_cast<std::size_t>(0);
        }
        return err<std::size_t>(error::from_errno());
    }

    for (int i = 0; i < ready_count; ++i) {
        events[static_cast<std::size_t>(i)] = ready_event{
            .fd = sys_events_ptr[static_cast<std::size_t>(i)].data.fd,
            .events = sys_events_ptr[static_cast<std::size_t>(i)].events};
    }
    return static_cast<std::size_t>(ready_count);
}

int reactor::native_handle() const noexcept {
    return epoll_fd_.get();
}

bool reactor::valid() const noexcept {
    return epoll_fd_.valid();
}

result<void> reactor::ctl(int operation, int fd, std::uint32_t events,
                          bool has_event) noexcept {
    if (!valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }
    if (fd < 0) {
        return err<void>(make_error_from_errno(EBADF));
    }

    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    epoll_event *event_ptr = has_event ? &event : nullptr;
    if (::epoll_ctl(epoll_fd_.get(), operation, fd, event_ptr) == 0) {
        return ok();
    }

    if (operation == EPOLL_CTL_DEL && errno == ENOENT) {
        return ok();
    }
    return err<void>(error::from_errno());
}

bool has_event(std::uint32_t event_mask, std::uint32_t flag) noexcept {
    return (event_mask & flag) != 0U;
}

} // namespace simplenet::epoll
