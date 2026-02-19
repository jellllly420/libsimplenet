#include "simplenet/blocking/endpoint.hpp"

namespace simplenet::blocking {

endpoint endpoint::loopback(std::uint16_t port) {
    return endpoint{.host = "127.0.0.1", .port = port};
}

endpoint endpoint::any(std::uint16_t port) {
    return endpoint{.host = "0.0.0.0", .port = port};
}

} // namespace simplenet::blocking
