#include "simplenet/simplenet.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> to_bytes(std::string_view value) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (const char ch : value) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
}

std::string from_bytes(std::span<const std::byte> bytes) {
    std::string text;
    text.reserve(bytes.size());
    for (const auto b : bytes) {
        text.push_back(static_cast<char>(b));
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 4) {
        std::cerr << "usage: simplenet_echo_client [host] [port] [payload]\n";
        return 2;
    }

    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    std::uint16_t port = 8080;
    if (argc > 2) {
        try {
            std::size_t consumed = 0;
            const auto text = std::string{argv[2]};
            const auto parsed = std::stoul(text, &consumed);
            if (consumed != text.size()) {
                std::cerr << "invalid port argument\n";
                return 2;
            }
            if (parsed == 0U || parsed > 65535U) {
                std::cerr << "port must be in range [1, 65535]\n";
                return 2;
            }
            port = static_cast<std::uint16_t>(parsed);
        } catch (const std::exception&) {
            std::cerr << "invalid port argument\n";
            return 2;
        }
    }
    const std::string payload = argc > 3 ? argv[3] : "hello libsimplenet";

    auto client_result = simplenet::blocking::tcp_stream::connect(
        simplenet::blocking::endpoint{host, port});
    if (!client_result.has_value()) {
        std::cerr << "connect failed: " << client_result.error().message()
                  << '\n';
        return 1;
    }

    auto client = std::move(client_result.value());
    const auto write_result =
        simplenet::blocking::write_all(client, to_bytes(payload));
    if (!write_result.has_value()) {
        std::cerr << "write failed: " << write_result.error().message() << '\n';
        return 1;
    }

    std::vector<std::byte> response(payload.size());
    const auto read_result = simplenet::blocking::read_exact(client, response);
    if (!read_result.has_value()) {
        std::cerr << "read failed: " << read_result.error().message() << '\n';
        return 1;
    }

    std::cout << from_bytes(response) << '\n';
    return 0;
}
