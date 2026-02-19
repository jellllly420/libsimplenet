#include "simplenet/runtime/engine.hpp"

#include <cerrno>

namespace simplenet::runtime {

engine::engine(backend choice, std::uint32_t uring_queue_depth)
    : backend_(choice) {
    if (backend_ == backend::epoll) {
        epoll_loop_.emplace();
        return;
    }

    uring_loop_.emplace(uring_queue_depth);
}

engine::backend engine::selected_backend() const noexcept {
    return backend_;
}

bool engine::valid() const noexcept {
    if (epoll_loop_.has_value()) {
        return epoll_loop_->valid();
    }
    if (uring_loop_.has_value()) {
        return uring_loop_->valid();
    }
    return false;
}

result<void> engine::run() noexcept {
    if (epoll_loop_.has_value()) {
        return epoll_loop_->run();
    }
    if (uring_loop_.has_value()) {
        return uring_loop_->run();
    }
    return err<void>(make_error_from_errno(EINVAL));
}

void engine::stop() noexcept {
    if (epoll_loop_.has_value()) {
        epoll_loop_->stop();
        return;
    }

    if (uring_loop_.has_value()) {
        uring_loop_->stop();
    }
}

} // namespace simplenet::runtime
