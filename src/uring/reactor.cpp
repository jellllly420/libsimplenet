#include "simplenet/uring/reactor.hpp"

#include <cerrno>
#include <chrono>
#include <new>

namespace {

void destroy_ring(io_uring *ring) noexcept {
    if (ring == nullptr) {
        return;
    }
    ::io_uring_queue_exit(ring);
    delete ring;
}

[[nodiscard]] __kernel_timespec
to_kernel_timespec(std::chrono::milliseconds timeout) noexcept {
    if (timeout < std::chrono::milliseconds{0}) {
        timeout = std::chrono::milliseconds{0};
    }

    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - seconds);

    __kernel_timespec ts{};
    ts.tv_sec = static_cast<decltype(ts.tv_sec)>(seconds.count());
    ts.tv_nsec = static_cast<decltype(ts.tv_nsec)>(nanoseconds.count());
    return ts;
}

} // namespace

namespace simplenet::uring {

reactor::reactor(std::unique_ptr<io_uring, void (*)(io_uring *)> ring) noexcept
    : ring_(std::move(ring)) {}

result<reactor> reactor::create(std::uint32_t entries) noexcept {
    if (entries == 0U) {
        return err<reactor>(make_error_from_errno(EINVAL));
    }

    std::unique_ptr<io_uring, void (*)(io_uring *)> ring{
        new (std::nothrow) io_uring{}, destroy_ring};
    if (ring == nullptr) {
        return err<reactor>(make_error_from_errno(ENOMEM));
    }

    const int init_result =
        ::io_uring_queue_init(static_cast<unsigned>(entries), ring.get(), 0U);
    if (init_result < 0) {
        return err<reactor>(make_error_from_errno(-init_result));
    }

    return reactor{std::move(ring)};
}

result<void> reactor::submit_poll_add(std::uint64_t user_data, int fd,
                                      std::uint32_t poll_mask) noexcept {
    if (!valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }
    if (user_data == 0U || fd < 0 || poll_mask == 0U) {
        return err<void>(make_error_from_errno(EINVAL));
    }

    io_uring_sqe *sqe = ::io_uring_get_sqe(ring_.get());
    if (sqe == nullptr) {
        return err<void>(make_error_from_errno(EBUSY));
    }

    ::io_uring_prep_poll_add(sqe, fd, poll_mask);
    ::io_uring_sqe_set_data64(sqe, user_data);
    return ok();
}

result<void>
reactor::submit_poll_remove(std::uint64_t target_user_data) noexcept {
    if (!valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }
    if (target_user_data == 0U) {
        return err<void>(make_error_from_errno(EINVAL));
    }

    io_uring_sqe *sqe = ::io_uring_get_sqe(ring_.get());
    if (sqe == nullptr) {
        return err<void>(make_error_from_errno(EBUSY));
    }

    ::io_uring_prep_poll_remove(sqe, target_user_data);
    ::io_uring_sqe_set_data64(sqe, 0U);
    return ok();
}

result<void> reactor::submit() noexcept {
    if (!valid()) {
        return err<void>(make_error_from_errno(EBADF));
    }

    const int submit_result = ::io_uring_submit(ring_.get());
    if (submit_result < 0) {
        return err<void>(make_error_from_errno(-submit_result));
    }
    return ok();
}

result<std::size_t>
reactor::wait(std::span<completion> completions,
              std::optional<std::chrono::milliseconds> timeout) noexcept {
    if (!valid()) {
        return err<std::size_t>(make_error_from_errno(EBADF));
    }
    if (completions.empty()) {
        return err<std::size_t>(make_error_from_errno(EINVAL));
    }

    io_uring_cqe *first_cqe = nullptr;
    int wait_result = 0;

    if (timeout.has_value()) {
        auto timeout_spec = to_kernel_timespec(timeout.value());
        wait_result =
            ::io_uring_wait_cqe_timeout(ring_.get(), &first_cqe, &timeout_spec);
        if (wait_result == -ETIME || wait_result == -EINTR) {
            return static_cast<std::size_t>(0);
        }
    } else {
        wait_result = ::io_uring_wait_cqe(ring_.get(), &first_cqe);
        if (wait_result == -EINTR) {
            return static_cast<std::size_t>(0);
        }
    }

    if (wait_result < 0) {
        return err<std::size_t>(make_error_from_errno(-wait_result));
    }

    std::size_t completion_count = 0;

    auto consume_cqe = [&](io_uring_cqe *cqe) {
        completions[completion_count] =
            completion{::io_uring_cqe_get_data64(cqe), cqe->res};
        ++completion_count;
        ::io_uring_cqe_seen(ring_.get(), cqe);
    };

    consume_cqe(first_cqe);

    while (completion_count < completions.size()) {
        io_uring_cqe *cqe = nullptr;
        const int peek_result = ::io_uring_peek_cqe(ring_.get(), &cqe);
        if (peek_result == -EAGAIN || cqe == nullptr) {
            break;
        }
        if (peek_result < 0) {
            return err<std::size_t>(make_error_from_errno(-peek_result));
        }
        consume_cqe(cqe);
    }

    return completion_count;
}

bool reactor::valid() const noexcept {
    return ring_ != nullptr;
}

} // namespace simplenet::uring
