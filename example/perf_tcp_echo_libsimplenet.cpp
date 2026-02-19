#include "simplenet/simplenet.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct options {
    std::size_t iterations{2000};
    std::size_t payload_size{1024};
    std::size_t connections{8};
};

bool parse_positive_size(const char* text, std::size_t& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0) {
        return false;
    }
    if (parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_args(int argc, char** argv, options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        if (arg == "--iterations" && i + 1 < argc) {
            if (!parse_positive_size(argv[++i], out.iterations)) {
                return false;
            }
            continue;
        }
        if (arg == "--payload-size" && i + 1 < argc) {
            if (!parse_positive_size(argv[++i], out.payload_size)) {
                return false;
            }
            continue;
        }
        if (arg == "--connections" && i + 1 < argc) {
            if (!parse_positive_size(argv[++i], out.connections)) {
                return false;
            }
            continue;
        }
        return false;
    }

    return true;
}

void record_failure(std::atomic_bool& failed,
                    std::mutex& error_mutex,
                    std::string& error_message,
                    std::string message) {
    bool expected = false;
    if (!failed.compare_exchange_strong(expected, true)) {
        return;
    }

    std::lock_guard<std::mutex> lock(error_mutex);
    error_message = std::move(message);
}

void run_echo_connection(simplenet::blocking::tcp_stream stream,
                         std::size_t iterations,
                         std::size_t payload_size,
                         std::atomic_bool& failed,
                         std::mutex& error_mutex,
                         std::string& error_message) {
    std::vector<std::byte> payload(payload_size);

    for (std::size_t i = 0; i < iterations; ++i) {
        if (failed.load()) {
            return;
        }

        const auto read_result = simplenet::blocking::read_exact(stream, payload);
        if (!read_result.has_value()) {
            record_failure(failed, error_mutex, error_message,
                           "server read failed: " + read_result.error().message());
            return;
        }

        const auto write_result = simplenet::blocking::write_all(stream, payload);
        if (!write_result.has_value()) {
            record_failure(failed, error_mutex, error_message,
                           "server write failed: " + write_result.error().message());
            return;
        }
    }
}

void print_usage() {
    std::cerr
        << "usage: simplenet_perf_tcp_echo_libsimplenet "
           "[--iterations N] [--payload-size N] [--connections N]\n";
}

} // namespace

int main(int argc, char** argv) {
    options opts{};
    if (!parse_args(argc, argv, opts)) {
        print_usage();
        return 2;
    }

    if (opts.iterations > (std::numeric_limits<std::size_t>::max() / opts.connections)) {
        std::cerr << "iterations * connections overflow\n";
        return 2;
    }

    auto listener_result = simplenet::blocking::tcp_listener::bind(
        simplenet::blocking::endpoint::loopback(0),
        static_cast<int>(opts.connections > 64 ? opts.connections : 64));
    if (!listener_result.has_value()) {
        std::cerr << "bind failed: " << listener_result.error().message() << "\n";
        return 1;
    }

    auto listener = std::move(listener_result.value());
    const auto port_result = listener.local_port();
    if (!port_result.has_value()) {
        std::cerr << "local_port failed: " << port_result.error().message() << "\n";
        return 1;
    }
    const std::uint16_t port = port_result.value();

    std::atomic_bool failed{false};
    std::mutex error_mutex;
    std::string error_message;

    std::thread server_thread([&]() {
        std::vector<std::thread> handlers;
        handlers.reserve(opts.connections);

        for (std::size_t c = 0; c < opts.connections; ++c) {
            if (failed.load()) {
                break;
            }

            auto accepted = listener.accept();
            if (!accepted.has_value()) {
                record_failure(failed, error_mutex, error_message,
                               "accept failed: " + accepted.error().message());
                break;
            }

            auto stream = std::move(accepted.value());
            handlers.emplace_back(
                [stream = std::move(stream), &opts, &failed, &error_mutex, &error_message]() mutable {
                    run_echo_connection(std::move(stream), opts.iterations,
                                        opts.payload_size, failed, error_mutex,
                                        error_message);
                });
        }

        for (auto& handler : handlers) {
            handler.join();
        }
    });

    std::vector<simplenet::blocking::tcp_stream> clients;
    clients.reserve(opts.connections);
    for (std::size_t c = 0; c < opts.connections; ++c) {
        auto connected = simplenet::blocking::tcp_stream::connect(
            simplenet::blocking::endpoint::loopback(port));
        if (!connected.has_value()) {
            record_failure(failed, error_mutex, error_message,
                           "connect failed: " + connected.error().message());
            break;
        }
        clients.push_back(std::move(connected.value()));
    }

    std::vector<std::byte> request(opts.payload_size, std::byte{0x42});
    std::vector<std::byte> response(opts.payload_size);

    const auto start = std::chrono::steady_clock::now();
    if (!failed.load()) {
        for (std::size_t i = 0; i < opts.iterations; ++i) {
            for (auto& client : clients) {
                auto write_result = simplenet::blocking::write_all(client, request);
                if (!write_result.has_value()) {
                    record_failure(failed, error_mutex, error_message,
                                   "client write failed: " + write_result.error().message());
                    break;
                }
            }
            if (failed.load()) {
                break;
            }

            for (auto& client : clients) {
                auto read_result = simplenet::blocking::read_exact(client, response);
                if (!read_result.has_value()) {
                    record_failure(failed, error_mutex, error_message,
                                   "client read failed: " + read_result.error().message());
                    break;
                }
            }
            if (failed.load()) {
                break;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();

    clients.clear();
    server_thread.join();

    if (failed.load()) {
        std::lock_guard<std::mutex> lock(error_mutex);
        std::cerr << "benchmark failed: " << error_message << "\n";
        return 1;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
    const double total_ms = elapsed.count() * 1000.0;
    const double total_s = elapsed.count();
    if (total_s <= 0.0) {
        std::cerr << "benchmark failed: non-positive runtime\n";
        return 1;
    }

    const std::size_t echoes = opts.iterations * opts.connections;
    const std::size_t total_bytes = echoes * opts.payload_size * 2;
    const double echoes_per_sec = static_cast<double>(echoes) / total_s;
    const double mb_per_sec = static_cast<double>(total_bytes) / 1'000'000.0 / total_s;

    std::cout << std::fixed << std::setprecision(3)
              << "PERF,impl=libsimplenet,scenario=tcp_echo,iterations=" << opts.iterations
              << ",payload_size=" << opts.payload_size << ",connections="
              << opts.connections << ",echoes=" << echoes << ",bytes="
              << total_bytes << ",total_ms=" << total_ms << ",echoes_per_sec="
              << echoes_per_sec << ",mb_per_sec=" << mb_per_sec << "\n";
    return 0;
}
