#include "simplenet/runtime/write_queue.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>

namespace {

using namespace std::chrono_literals;

} // namespace

namespace simplenet::runtime {

queued_writer::queued_writer(simplenet::nonblocking::tcp_stream stream,
                             watermarks marks)
    : stream_(std::move(stream)), marks_(marks) {
    if (marks_.low == 0) {
        marks_.low = 1;
    }
    if (marks_.high < marks_.low) {
        marks_.high = marks_.low;
    }
}

result<backpressure_state>
queued_writer::enqueue(std::span<const std::byte> bytes) {
    std::vector<std::byte> copy(bytes.begin(), bytes.end());
    return enqueue_owned(std::move(copy));
}

result<backpressure_state>
queued_writer::enqueue(std::vector<std::byte>&& bytes) {
    return enqueue_owned(std::move(bytes));
}

result<backpressure_state>
queued_writer::enqueue_owned(std::vector<std::byte>&& bytes) {
    if (!stream_.valid()) {
        return err<backpressure_state>(make_error_from_errno(EBADF));
    }

    if (bytes.empty()) {
        return high_watermark_active_ ? backpressure_state::high_watermark
                                      : backpressure_state::normal;
    }

    if (high_watermark_active_ && queued_bytes_ >= marks_.low) {
        return err<backpressure_state>(make_error_from_errno(EWOULDBLOCK));
    }

    queued_bytes_ += bytes.size();
    queue_.push_back(std::move(bytes));

    if (queued_bytes_ >= marks_.high) {
        high_watermark_active_ = true;
    }

    return high_watermark_active_ ? backpressure_state::high_watermark
                                  : backpressure_state::normal;
}

task<result<void>> queued_writer::flush(std::chrono::milliseconds timeout,
                                        cancel_token token) {
    if (timeout < std::chrono::milliseconds{0}) {
        co_return err<void>(make_error_from_errno(EINVAL));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (queued_bytes_ > 0) {
        if (token.stop_requested()) {
            co_return err<void>(make_error_from_errno(ECANCELED));
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            co_return err<void>(make_error_from_errno(ETIMEDOUT));
        }

        auto& front = queue_.front();
        const auto remaining_bytes = front.size() - front_offset_;
        const auto remaining_span = std::span<const std::byte>{
            front.data() + front_offset_, remaining_bytes};

        const auto remaining_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                  now);
        const auto slice = std::max<std::chrono::milliseconds>(
            1ms, std::min<std::chrono::milliseconds>(remaining_time, 100ms));

        auto write_result = co_await async_write_some_with_timeout(
            stream_, remaining_span, slice, token);
        if (!write_result.has_value()) {
            co_return err<void>(write_result.error());
        }
        if (write_result.value() == 0U) {
            co_return err<void>(make_error_from_errno(EPIPE));
        }

        front_offset_ += write_result.value();
        queued_bytes_ -= write_result.value();
        if (front_offset_ == front.size()) {
            queue_.pop_front();
            front_offset_ = 0;
        }
        update_backpressure_after_drain();
    }

    co_return ok();
}

task<result<void>>
queued_writer::graceful_shutdown(std::chrono::milliseconds timeout,
                                 cancel_token token) {
    const auto flush_result = co_await flush(timeout, token);
    if (!flush_result.has_value()) {
        co_return flush_result;
    }

    const auto shutdown_result = stream_.shutdown_write();
    if (!shutdown_result.has_value()) {
        co_return shutdown_result;
    }
    co_return ok();
}

std::size_t queued_writer::queued_bytes() const noexcept {
    return queued_bytes_;
}

bool queued_writer::high_watermark_active() const noexcept {
    return high_watermark_active_;
}

int queued_writer::native_handle() const noexcept {
    return stream_.native_handle();
}

void queued_writer::update_backpressure_after_drain() noexcept {
    if (high_watermark_active_ && queued_bytes_ <= marks_.low) {
        high_watermark_active_ = false;
    }
}

} // namespace simplenet::runtime
