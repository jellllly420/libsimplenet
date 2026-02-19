#include "simplenet/core/error.hpp"

namespace simplenet {

error::error(std::error_code code) noexcept : code_(code) {}

error error::from_errno() noexcept {
    return error{std::error_code{errno, std::system_category()}};
}

error error::from_errno(int value) noexcept {
    return error{std::error_code{value, std::system_category()}};
}

std::error_code error::code() const noexcept {
    return code_;
}

int error::value() const noexcept {
    return code_.value();
}

std::string error::message() const {
    return code_.message();
}

error make_error_from_errno(int value) noexcept {
    return error::from_errno(value);
}

} // namespace simplenet
