#include "simplenet/simplenet.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct options {
    std::size_t iterations{2000};
    std::size_t payload_size{1024};
    std::size_t connections{8};
    simplenet::runtime::engine::backend backend{
        simplenet::runtime::engine::backend::epoll};
    std::uint32_t uring_queue_depth{512};
};

class posix_socket {
public:
    posix_socket() = default;
    explicit posix_socket(int fd) : fd_(fd) {}

    posix_socket(const posix_socket&) = delete;
    posix_socket& operator=(const posix_socket&) = delete;

    posix_socket(posix_socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    posix_socket& operator=(posix_socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~posix_socket() { close(); }

    [[nodiscard]] int native_handle() const { return fd_; }

private:
    void close() {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    int fd_{-1};
};

std::string posix_error_message(int error_number) {
    return std::error_code(error_number, std::generic_category()).message();
}

bool connect_loopback_socket(std::uint16_t port,
                             posix_socket& out,
                             int& error_number) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error_number = errno;
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    while (true) {
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&address),
                      sizeof(address)) == 0) {
            out = posix_socket(fd);
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        error_number = errno;
        static_cast<void>(::close(fd));
        return false;
    }
}

bool write_all_socket(const posix_socket& socket,
                      std::span<const std::byte> payload,
                      int& error_number) {
    const auto* buffer = reinterpret_cast<const char*>(payload.data());
    std::size_t offset = 0;

    while (offset < payload.size()) {
        const auto* current = buffer + offset;
        const auto remaining = payload.size() - offset;
        const ssize_t wrote = ::send(socket.native_handle(), current, remaining, 0);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            error_number = errno;
            return false;
        }
        if (wrote == 0) {
            error_number = EPIPE;
            return false;
        }
        offset += static_cast<std::size_t>(wrote);
    }

    return true;
}

bool read_exact_socket(const posix_socket& socket,
                       std::span<std::byte> payload,
                       int& error_number) {
    auto* buffer = reinterpret_cast<char*>(payload.data());
    std::size_t offset = 0;

    while (offset < payload.size()) {
        auto* current = buffer + offset;
        const auto remaining = payload.size() - offset;
        const ssize_t read_count = ::recv(socket.native_handle(), current, remaining, 0);
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            error_number = errno;
            return false;
        }
        if (read_count == 0) {
            error_number = ECONNRESET;
            return false;
        }
        offset += static_cast<std::size_t>(read_count);
    }

    return true;
}

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
    if (parsed >
        static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_positive_u32(const char* text, std::uint32_t& value) {
    std::size_t parsed = 0;
    if (!parse_positive_size(text, parsed)) {
        return false;
    }
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_backend(std::string_view arg, simplenet::runtime::engine::backend& out) {
    if (arg == "epoll") {
        out = simplenet::runtime::engine::backend::epoll;
        return true;
    }
    if (arg == "io_uring") {
        out = simplenet::runtime::engine::backend::io_uring;
        return true;
    }
    return false;
}

const char* backend_name(simplenet::runtime::engine::backend backend) {
    if (backend == simplenet::runtime::engine::backend::io_uring) {
        return "io_uring";
    }
    return "epoll";
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
        if (arg == "--backend" && i + 1 < argc) {
            if (!parse_backend(argv[++i], out.backend)) {
                return false;
            }
            continue;
        }
        if (arg == "--uring-queue-depth" && i + 1 < argc) {
            if (!parse_positive_u32(argv[++i], out.uring_queue_depth)) {
                return false;
            }
            continue;
        }
        return false;
    }

    return true;
}

void print_usage() {
    std::cerr
        << "usage: simplenet_perf_async_echo_libsimplenet "
           "[--iterations N] [--payload-size N] [--connections N] "
           "[--backend epoll|io_uring] [--uring-queue-depth N]\n";
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

simplenet::runtime::task<void> run_echo_session(
    simplenet::nonblocking::tcp_stream stream, std::size_t iterations,
    std::size_t payload_size, std::atomic_bool& failed, std::mutex& error_mutex,
    std::string& error_message) {
    std::vector<std::byte> payload(payload_size);

    for (std::size_t i = 0; i < iterations; ++i) {
        if (failed.load(std::memory_order_acquire)) {
            co_return;
        }

        auto read_result = co_await simplenet::runtime::async_read_exact(
            stream, std::span<std::byte>{payload.data(), payload.size()});
        if (!read_result.has_value()) {
            record_failure(failed, error_mutex, error_message,
                           "server async_read_exact failed: " +
                               read_result.error().message());
            co_return;
        }

        auto write_result = co_await simplenet::runtime::async_write_all(
            stream, std::span<const std::byte>{payload.data(), payload.size()});
        if (!write_result.has_value()) {
            record_failure(failed, error_mutex, error_message,
                           "server async_write_all failed: " +
                               write_result.error().message());
            co_return;
        }
    }

    co_return;
}

simplenet::runtime::task<void> run_accept_loop(
    simplenet::io_context& context, simplenet::nonblocking::tcp_listener& listener,
    std::size_t connections, std::size_t iterations, std::size_t payload_size,
    std::atomic_bool& failed, std::mutex& error_mutex, std::string& error_message,
    std::atomic_size_t& accepted_count) {
    for (std::size_t c = 0; c < connections; ++c) {
        if (failed.load(std::memory_order_acquire)) {
            co_return;
        }

        auto accepted = co_await simplenet::runtime::async_accept(listener);
        if (!accepted.has_value()) {
            record_failure(failed, error_mutex, error_message,
                           "server async_accept failed: " +
                               accepted.error().message());
            co_return;
        }

        accepted_count.fetch_add(1, std::memory_order_relaxed);
        context.spawn(run_echo_session(std::move(accepted.value()), iterations,
                                       payload_size, failed, error_mutex,
                                       error_message));
    }

    co_return;
}

} // namespace

int main(int argc, char** argv) {
    options opts{};
    if (!parse_args(argc, argv, opts)) {
        print_usage();
        return 2;
    }

    if (opts.iterations >
        (std::numeric_limits<std::size_t>::max() / opts.connections)) {
        std::cerr << "iterations * connections overflow\n";
        return 2;
    }

    simplenet::io_context context{opts.backend, opts.uring_queue_depth};
    if (!context.valid()) {
        std::cerr << "backend unavailable: " << backend_name(opts.backend) << "\n";
        return 3;
    }

    auto listener_result = simplenet::nonblocking::tcp_listener::bind(
        simplenet::nonblocking::endpoint::loopback(0),
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
    std::atomic_size_t accepted_count{0};

    context.spawn(run_accept_loop(context, listener, opts.connections,
                                  opts.iterations, opts.payload_size, failed,
                                  error_mutex, error_message, accepted_count));

    std::promise<simplenet::result<void>> run_promise;
    auto run_future = run_promise.get_future();
    std::thread runtime_thread([&]() { run_promise.set_value(context.run()); });

    std::vector<posix_socket> clients;
    clients.reserve(opts.connections);
    for (std::size_t c = 0; c < opts.connections; ++c) {
        int error_number = 0;
        posix_socket client;
        if (!connect_loopback_socket(port, client, error_number)) {
            record_failure(failed, error_mutex, error_message,
                           "client connect failed: " +
                               posix_error_message(error_number));
            break;
        }
        clients.push_back(std::move(client));
    }

    std::vector<std::byte> request(opts.payload_size, std::byte{0x42});
    std::vector<std::byte> response(opts.payload_size);

    const auto start = std::chrono::steady_clock::now();
    if (!failed.load(std::memory_order_acquire)) {
        for (std::size_t i = 0; i < opts.iterations; ++i) {
            for (auto& client : clients) {
                int error_number = 0;
                if (!write_all_socket(
                        client,
                        std::span<const std::byte>{request.data(), request.size()},
                        error_number)) {
                    record_failure(failed, error_mutex, error_message,
                                   "client send failed: " +
                                       posix_error_message(error_number));
                    break;
                }
            }
            if (failed.load(std::memory_order_acquire)) {
                break;
            }

            for (auto& client : clients) {
                int error_number = 0;
                if (!read_exact_socket(
                        client, std::span<std::byte>{response.data(), response.size()},
                        error_number)) {
                    record_failure(failed, error_mutex, error_message,
                                   "client recv failed: " +
                                       posix_error_message(error_number));
                    break;
                }
            }
            if (failed.load(std::memory_order_acquire)) {
                break;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();

    clients.clear();
    context.stop();
    runtime_thread.join();

    const auto run_status = run_future.get();
    if (!run_status.has_value() && !failed.load(std::memory_order_acquire)) {
        record_failure(failed, error_mutex, error_message,
                       "runtime run failed: " + run_status.error().message());
    }

    if (accepted_count.load(std::memory_order_relaxed) != opts.connections &&
        !failed.load(std::memory_order_acquire)) {
        record_failure(failed, error_mutex, error_message,
                       "accepted connection count mismatch");
    }

    if (failed.load(std::memory_order_acquire)) {
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
              << "PERF,impl=libsimplenet,scenario=async_tcp_echo,backend="
              << backend_name(opts.backend) << ",iterations=" << opts.iterations
              << ",payload_size=" << opts.payload_size << ",connections="
              << opts.connections << ",echoes=" << echoes << ",bytes="
              << total_bytes << ",uring_queue_depth=" << opts.uring_queue_depth
              << ",total_ms=" << total_ms << ",echoes_per_sec=" << echoes_per_sec
              << ",mb_per_sec=" << mb_per_sec << "\n";
    return 0;
}
