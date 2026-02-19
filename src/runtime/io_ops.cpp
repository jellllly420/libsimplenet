#include "simplenet/runtime/io_ops.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <optional>
#include <sys/timerfd.h>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

[[nodiscard]] int get_sleep_timerfd() noexcept {
    struct SleepTimerFd {
        simplenet::unique_fd fd{};
        SleepTimerFd() {
            const int raw =
                ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
            if (raw >= 0) {
                fd = simplenet::unique_fd{raw};
            }
        }
    };
    thread_local SleepTimerFd state{};
    return state.fd.get();
}

class readiness_wait_awaitable {
public:
    readiness_wait_awaitable(int fd, bool readable,
                             std::optional<std::chrono::milliseconds> timeout,
                             simplenet::error timeout_error) noexcept
        : fd_(fd), readable_(readable), timeout_(timeout),
          timeout_error_(timeout_error) {}

    [[nodiscard]] bool await_ready() const noexcept {
        return false;
    }

    template <class Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        if constexpr (!requires(Promise& p) { p.scheduler_ptr(); }) {
            status_ = simplenet::err<void>(simplenet::make_error_from_errno(EINVAL));
            return false;
        } else {
            scheduler_ = handle.promise().scheduler_ptr();
            handle_ = handle;

            if (scheduler_ == nullptr) {
                status_ = simplenet::err<void>(simplenet::make_error_from_errno(EINVAL));
                return false;
            }

            status_ = readable_
                          ? scheduler_->wait_for_readable(fd_, handle, timeout_,
                                                          timeout_error_)
                          : scheduler_->wait_for_writable(fd_, handle, timeout_,
                                                          timeout_error_);
            return status_.has_value();
        }
    }

    [[nodiscard]] simplenet::result<void> await_resume() noexcept {
        if (!status_.has_value()) {
            return status_;
        }

        if (scheduler_ == nullptr || !handle_) {
            return status_;
        }
        return scheduler_->consume_wait_result(handle_);
    }

private:
    int fd_{-1};
    bool readable_{true};
    std::optional<std::chrono::milliseconds> timeout_{};
    simplenet::error timeout_error_{simplenet::make_error_from_errno(ETIMEDOUT)};
    simplenet::runtime::scheduler *scheduler_{nullptr};
    std::coroutine_handle<> handle_{};
    simplenet::result<void> status_{simplenet::ok()};
};

[[nodiscard]] bool is_timeout_error(const simplenet::error& err) noexcept {
    return err.value() == ETIMEDOUT;
}

} // namespace

namespace simplenet::runtime {

task<result<void>> wait_readable(int fd) {
    const auto status = co_await readiness_wait_awaitable{
        fd, true, std::nullopt, make_error_from_errno(ETIMEDOUT)};
    co_return status;
}

task<result<void>> wait_writable(int fd) {
    const auto status = co_await readiness_wait_awaitable{
        fd, false, std::nullopt, make_error_from_errno(ETIMEDOUT)};
    co_return status;
}

task<result<void>> wait_readable_for(int fd,
                                     std::chrono::milliseconds timeout) {
    const auto status = co_await readiness_wait_awaitable{
        fd, true, timeout, make_error_from_errno(ETIMEDOUT)};
    co_return status;
}

task<result<void>> wait_writable_for(int fd,
                                     std::chrono::milliseconds timeout) {
    const auto status = co_await readiness_wait_awaitable{
        fd, false, timeout, make_error_from_errno(ETIMEDOUT)};
    co_return status;
}

task<result<simplenet::nonblocking::tcp_stream>>
async_accept(simplenet::nonblocking::tcp_listener& listener) {
    while (true) {
        auto accept_result = listener.accept();
        if (accept_result.has_value()) {
            co_return accept_result;
        }

        if (!simplenet::nonblocking::is_would_block(accept_result.error())) {
            co_return err<simplenet::nonblocking::tcp_stream>(accept_result.error());
        }

        const auto wait_result =
            co_await wait_readable(listener.native_handle());
        if (!wait_result.has_value()) {
            co_return err<simplenet::nonblocking::tcp_stream>(wait_result.error());
        }
    }
}

task<result<simplenet::nonblocking::tcp_stream>>
async_connect(const simplenet::nonblocking::endpoint& endpoint) {
    auto stream_result = simplenet::nonblocking::tcp_stream::connect(endpoint);
    if (!stream_result.has_value()) {
        co_return err<simplenet::nonblocking::tcp_stream>(stream_result.error());
    }

    auto stream = std::move(stream_result.value());

    while (true) {
        const auto finish_result = stream.finish_connect();
        if (finish_result.has_value()) {
            co_return std::move(stream);
        }

        if (!simplenet::nonblocking::is_in_progress(finish_result.error()) &&
            !simplenet::nonblocking::is_would_block(finish_result.error())) {
            co_return err<simplenet::nonblocking::tcp_stream>(finish_result.error());
        }

        const auto wait_result = co_await wait_writable(stream.native_handle());
        if (!wait_result.has_value()) {
            co_return err<simplenet::nonblocking::tcp_stream>(wait_result.error());
        }
    }
}

task<result<std::size_t>> async_read_some(simplenet::nonblocking::tcp_stream& stream,
                                          std::span<std::byte> buffer) {
    while (true) {
        auto read_result = stream.read_some(buffer);
        if (read_result.has_value()) {
            co_return read_result;
        }

        if (!simplenet::nonblocking::is_would_block(read_result.error())) {
            co_return err<std::size_t>(read_result.error());
        }

        const auto wait_result = co_await wait_readable(stream.native_handle());
        if (!wait_result.has_value()) {
            co_return err<std::size_t>(wait_result.error());
        }
    }
}

task<result<std::size_t>> async_write_some(simplenet::nonblocking::tcp_stream& stream,
                                           std::span<const std::byte> buffer) {
    while (true) {
        auto write_result = stream.write_some(buffer);
        if (write_result.has_value()) {
            co_return write_result;
        }

        if (!simplenet::nonblocking::is_would_block(write_result.error())) {
            co_return err<std::size_t>(write_result.error());
        }

        const auto wait_result = co_await wait_writable(stream.native_handle());
        if (!wait_result.has_value()) {
            co_return err<std::size_t>(wait_result.error());
        }
    }
}

task<result<void>> async_read_exact(simplenet::nonblocking::tcp_stream& stream,
                                    std::span<std::byte> buffer) {
    std::size_t total = 0;
    while (total < buffer.size()) {
        auto r = co_await async_read_some(stream, buffer.subspan(total));
        if (!r.has_value()) {
            co_return err<void>(r.error());
        }
        if (r.value() == 0U) {
            co_return err<void>(make_error_from_errno(ECONNRESET));
        }
        total += r.value();
    }
    co_return ok();
}

task<result<void>> async_write_all(simplenet::nonblocking::tcp_stream& stream,
                                   std::span<const std::byte> buffer) {
    std::size_t total = 0;
    while (total < buffer.size()) {
        auto r = co_await async_write_some(stream, buffer.subspan(total));
        if (!r.has_value()) {
            co_return err<void>(r.error());
        }
        if (r.value() == 0U) {
            co_return err<void>(make_error_from_errno(EPIPE));
        }
        total += r.value();
    }
    co_return ok();
}

task<result<void>> async_sleep(std::chrono::milliseconds duration,
                               cancel_token token) {
    if (token.stop_requested()) {
        co_return err<void>(make_error_from_errno(ECANCELED));
    }
    if (duration <= std::chrono::milliseconds{0}) {
        co_return ok();
    }

    const int timer_fd = get_sleep_timerfd();
    if (timer_fd < 0) {
        co_return err<void>(error::from_errno());
    }

    const auto deadline = std::chrono::steady_clock::now() + duration;

    while (true) {
        if (token.stop_requested()) {
            co_return err<void>(make_error_from_errno(ECANCELED));
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return ok();
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                  now);
        const auto slice = std::max<std::chrono::milliseconds>(
            std::chrono::milliseconds{1},
            std::min<std::chrono::milliseconds>(remaining, 20ms));

        ::itimerspec spec{};
        spec.it_value.tv_sec = static_cast<std::time_t>(slice.count() / 1000);
        spec.it_value.tv_nsec =
            static_cast<long>((slice.count() % 1000) * 1000000L);
        if (::timerfd_settime(timer_fd, 0, &spec, nullptr) != 0) {
            co_return err<void>(error::from_errno());
        }

        const auto wait_result = co_await wait_readable(timer_fd);
        if (!wait_result.has_value()) {
            co_return err<void>(wait_result.error());
        }

        std::uint64_t expirations = 0;
        const auto read_count =
            ::read(timer_fd, &expirations, sizeof(expirations));
        if (read_count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            co_return err<void>(error::from_errno());
        }
    }
}

task<result<std::size_t>> async_read_some_with_timeout(
    simplenet::nonblocking::tcp_stream& stream, std::span<std::byte> buffer,
    std::chrono::milliseconds timeout, cancel_token token) {
    if (timeout < std::chrono::milliseconds{0}) {
        co_return err<std::size_t>(make_error_from_errno(EINVAL));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        if (token.stop_requested()) {
            co_return err<std::size_t>(make_error_from_errno(ECANCELED));
        }

        auto read_result = stream.read_some(buffer);
        if (read_result.has_value()) {
            co_return read_result;
        }

        if (!simplenet::nonblocking::is_would_block(read_result.error())) {
            co_return err<std::size_t>(read_result.error());
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return err<std::size_t>(make_error_from_errno(ETIMEDOUT));
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                  now);
        const auto slice = std::max<std::chrono::milliseconds>(
            std::chrono::milliseconds{1},
            std::min<std::chrono::milliseconds>(remaining, 20ms));

        const auto wait_result =
            co_await wait_readable_for(stream.native_handle(), slice);
        if (!wait_result.has_value() &&
            !is_timeout_error(wait_result.error())) {
            co_return err<std::size_t>(wait_result.error());
        }
    }
}

task<result<std::size_t>> async_write_some_with_timeout(
    simplenet::nonblocking::tcp_stream& stream, std::span<const std::byte> buffer,
    std::chrono::milliseconds timeout, cancel_token token) {
    if (timeout < std::chrono::milliseconds{0}) {
        co_return err<std::size_t>(make_error_from_errno(EINVAL));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        if (token.stop_requested()) {
            co_return err<std::size_t>(make_error_from_errno(ECANCELED));
        }

        auto write_result = stream.write_some(buffer);
        if (write_result.has_value()) {
            co_return write_result;
        }

        if (!simplenet::nonblocking::is_would_block(write_result.error())) {
            co_return err<std::size_t>(write_result.error());
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return err<std::size_t>(make_error_from_errno(ETIMEDOUT));
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                  now);
        const auto slice = std::max<std::chrono::milliseconds>(
            std::chrono::milliseconds{1},
            std::min<std::chrono::milliseconds>(remaining, 20ms));

        const auto wait_result =
            co_await wait_writable_for(stream.native_handle(), slice);
        if (!wait_result.has_value() &&
            !is_timeout_error(wait_result.error())) {
            co_return err<std::size_t>(wait_result.error());
        }
    }
}

} // namespace simplenet::runtime
