#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

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
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using tcp = boost::asio::ip::tcp;

struct options {
    std::size_t iterations{2000};
    std::size_t payload_size{1024};
    std::size_t connections{8};
};

struct shared_state {
    options opts{};
    std::atomic_bool failed{false};
    std::atomic_size_t accepted_count{0};
    std::mutex error_mutex{};
    std::string error_message{};
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

void print_usage() {
    std::cerr
        << "usage: simplenet_perf_async_echo_boost_asio "
           "[--iterations N] [--payload-size N] [--connections N]\n";
}

void record_failure(const std::shared_ptr<shared_state>& state,
                    std::string message) {
    bool expected = false;
    if (!state->failed.compare_exchange_strong(expected, true)) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->error_mutex);
    state->error_message = std::move(message);
}

boost::asio::awaitable<void> run_echo_session(tcp::socket socket,
                                              std::shared_ptr<shared_state> state) {
    std::vector<char> payload(state->opts.payload_size);
    boost::system::error_code ec;

    for (std::size_t i = 0; i < state->opts.iterations; ++i) {
        if (state->failed.load(std::memory_order_acquire)) {
            co_return;
        }

        const std::size_t read_count = co_await boost::asio::async_read(
            socket, boost::asio::buffer(payload),
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec || read_count != payload.size()) {
            record_failure(state, "server async_read failed: " + ec.message());
            co_return;
        }

        const std::size_t write_count = co_await boost::asio::async_write(
            socket, boost::asio::buffer(payload),
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec || write_count != payload.size()) {
            record_failure(state, "server async_write failed: " + ec.message());
            co_return;
        }
    }

    co_return;
}

boost::asio::awaitable<void> run_accept_loop(tcp::acceptor& acceptor,
                                             std::shared_ptr<shared_state> state) {
    auto executor = co_await boost::asio::this_coro::executor;
    boost::system::error_code ec;

    for (std::size_t c = 0; c < state->opts.connections; ++c) {
        if (state->failed.load(std::memory_order_acquire)) {
            co_return;
        }

        tcp::socket socket = co_await acceptor.async_accept(
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec) {
            record_failure(state, "server async_accept failed: " + ec.message());
            co_return;
        }

        state->accepted_count.fetch_add(1, std::memory_order_relaxed);
        boost::asio::co_spawn(executor, run_echo_session(std::move(socket), state),
                              boost::asio::detached);
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

    boost::asio::io_context io;
    tcp::acceptor acceptor(io);
    boost::system::error_code ec;
    acceptor.open(tcp::v4(), ec);
    if (ec) {
        std::cerr << "acceptor open failed: " << ec.message() << "\n";
        return 1;
    }
    acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        std::cerr << "acceptor setup failed: " << ec.message() << "\n";
        return 1;
    }
    acceptor.bind(tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0), ec);
    if (ec) {
        std::cerr << "bind failed: " << ec.message() << "\n";
        return 1;
    }
    acceptor.listen(static_cast<int>(opts.connections > 64 ? opts.connections : 64),
                    ec);
    if (ec) {
        std::cerr << "listen failed: " << ec.message() << "\n";
        return 1;
    }

    const std::uint16_t port = acceptor.local_endpoint(ec).port();
    if (ec) {
        std::cerr << "local_endpoint failed: " << ec.message() << "\n";
        return 1;
    }

    auto state = std::make_shared<shared_state>();
    state->opts = opts;

    boost::asio::co_spawn(io, run_accept_loop(acceptor, state),
                          boost::asio::detached);

    std::thread io_thread([&]() { io.run(); });

    std::vector<posix_socket> clients;
    clients.reserve(opts.connections);
    for (std::size_t c = 0; c < opts.connections; ++c) {
        int error_number = 0;
        posix_socket client;
        if (!connect_loopback_socket(port, client, error_number)) {
            record_failure(state, "client connect failed: " +
                                      posix_error_message(error_number));
            break;
        }
        clients.push_back(std::move(client));
    }

    std::vector<std::byte> request(opts.payload_size, std::byte{0x42});
    std::vector<std::byte> response(opts.payload_size);

    const auto start = std::chrono::steady_clock::now();
    if (!state->failed.load(std::memory_order_acquire)) {
        for (std::size_t i = 0; i < opts.iterations; ++i) {
            for (auto& client : clients) {
                int error_number = 0;
                if (!write_all_socket(
                        client,
                        std::span<const std::byte>{request.data(), request.size()},
                        error_number)) {
                    record_failure(state, "client send failed: " +
                                              posix_error_message(error_number));
                    break;
                }
            }
            if (state->failed.load(std::memory_order_acquire)) {
                break;
            }

            for (auto& client : clients) {
                int error_number = 0;
                if (!read_exact_socket(
                        client, std::span<std::byte>{response.data(), response.size()},
                        error_number)) {
                    record_failure(state, "client recv failed: " +
                                              posix_error_message(error_number));
                    break;
                }
            }
            if (state->failed.load(std::memory_order_acquire)) {
                break;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();

    clients.clear();
    io.stop();
    io_thread.join();

    if (state->accepted_count.load(std::memory_order_relaxed) != opts.connections &&
        !state->failed.load(std::memory_order_acquire)) {
        record_failure(state, "accepted connection count mismatch");
    }

    if (state->failed.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(state->error_mutex);
        std::cerr << "benchmark failed: " << state->error_message << "\n";
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
              << "PERF,impl=boost_asio,scenario=async_tcp_echo,backend=epoll"
              << ",iterations=" << opts.iterations
              << ",payload_size=" << opts.payload_size << ",connections="
              << opts.connections << ",echoes=" << echoes << ",bytes="
              << total_bytes << ",total_ms=" << total_ms << ",echoes_per_sec="
              << echoes_per_sec << ",mb_per_sec=" << mb_per_sec << "\n";
    return 0;
}
