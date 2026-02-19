#include <boost/asio.hpp>

#include <array>
#include <atomic>
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

using tcp = boost::asio::ip::tcp;

struct options {
    std::size_t iterations{2000};
    std::size_t connections{32};
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
        << "usage: perf_connection_churn_boost_asio "
           "[--iterations N] [--connections N]\n";
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
    const std::size_t total_connections = opts.iterations * opts.connections;

    boost::asio::io_context io;
    boost::system::error_code ec;

    tcp::acceptor acceptor(io);
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
    acceptor.bind(
        tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0),
        ec);
    if (ec) {
        std::cerr << "bind failed: " << ec.message() << "\n";
        return 1;
    }
    acceptor.listen(static_cast<int>(opts.connections > 64 ? opts.connections : 64), ec);
    if (ec) {
        std::cerr << "listen failed: " << ec.message() << "\n";
        return 1;
    }

    const std::uint16_t port = acceptor.local_endpoint(ec).port();
    if (ec) {
        std::cerr << "local_endpoint failed: " << ec.message() << "\n";
        return 1;
    }

    std::atomic_bool failed{false};
    std::mutex error_mutex;
    std::string error_message;

    std::thread server_thread([&]() {
        std::array<char, 1> token{};

        for (std::size_t i = 0; i < total_connections; ++i) {
            if (failed.load()) {
                return;
            }

            tcp::socket socket(io);
            acceptor.accept(socket, ec);
            if (ec) {
                record_failure(failed, error_mutex, error_message,
                               "accept failed: " + ec.message());
                return;
            }

            const std::size_t read_count =
                boost::asio::read(socket, boost::asio::buffer(token), ec);
            if (ec || read_count != token.size()) {
                record_failure(failed, error_mutex, error_message,
                               "server read failed: " + ec.message());
                return;
            }

            const std::size_t write_count =
                boost::asio::write(socket, boost::asio::buffer(token), ec);
            if (ec || write_count != token.size()) {
                record_failure(failed, error_mutex, error_message,
                               "server write failed: " + ec.message());
                return;
            }
        }
    });

    tcp::endpoint endpoint(boost::asio::ip::address_v4::loopback(), port);
    std::array<char, 1> token{0x7f};

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < opts.iterations; ++iteration) {
        if (failed.load()) {
            break;
        }

        std::vector<tcp::socket> clients;
        clients.reserve(opts.connections);

        for (std::size_t c = 0; c < opts.connections; ++c) {
            clients.emplace_back(io);
            tcp::socket& socket = clients.back();
            socket.connect(endpoint, ec);
            if (ec) {
                record_failure(failed, error_mutex, error_message,
                               "connect failed: " + ec.message());
                clients.pop_back();
                break;
            }
        }
        if (failed.load()) {
            break;
        }

        for (auto& client : clients) {
            const std::size_t write_count =
                boost::asio::write(client, boost::asio::buffer(token), ec);
            if (ec || write_count != token.size()) {
                record_failure(failed, error_mutex, error_message,
                               "client write failed: " + ec.message());
                break;
            }
        }
        if (failed.load()) {
            break;
        }

        for (auto& client : clients) {
            const std::size_t read_count =
                boost::asio::read(client, boost::asio::buffer(token), ec);
            if (ec || read_count != token.size()) {
                record_failure(failed, error_mutex, error_message,
                               "client read failed: " + ec.message());
                break;
            }
        }
        if (failed.load()) {
            break;
        }
    }
    const auto end = std::chrono::steady_clock::now();

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

    const std::size_t total_bytes = total_connections * 2;
    const double connections_per_sec = static_cast<double>(total_connections) / total_s;

    std::cout << std::fixed << std::setprecision(3)
              << "PERF,impl=boost_asio,scenario=connection_churn,iterations="
              << opts.iterations << ",connections=" << opts.connections
              << ",total_connections=" << total_connections << ",bytes="
              << total_bytes << ",total_ms=" << total_ms
              << ",connections_per_sec=" << connections_per_sec << "\n";

    return 0;
}
