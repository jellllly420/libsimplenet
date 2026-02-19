#include "simplenet/core/unique_fd.hpp"

#include <cerrno>
#include <unistd.h>
#include <utility>

namespace simplenet {

unique_fd::unique_fd(int fd) noexcept : fd_(fd) {}

unique_fd::~unique_fd() noexcept {
    reset();
}

unique_fd::unique_fd(unique_fd&& other) noexcept : fd_(other.release()) {}

unique_fd& unique_fd::operator=(unique_fd&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int unique_fd::get() const noexcept {
    return fd_;
}

bool unique_fd::valid() const noexcept {
    return fd_ >= 0;
}

unique_fd::operator bool() const noexcept {
    return valid();
}

int unique_fd::release() noexcept {
    const int old_fd = fd_;
    fd_ = -1;
    return old_fd;
}

void unique_fd::reset(int fd) noexcept {
    if (fd_ == fd) {
        return;
    }
    if (valid()) {
        (void)::close(fd_);
    }
    fd_ = fd;
}

void unique_fd::swap(unique_fd& other) noexcept {
    std::swap(fd_, other.fd_);
}

result<void> close_fd(int fd) noexcept {
    if (fd < 0) {
        return err<void>(make_error_from_errno(EBADF));
    }

    if (::close(fd) == 0) {
        return ok();
    }

    return err<void>(error::from_errno());
}

} // namespace simplenet
