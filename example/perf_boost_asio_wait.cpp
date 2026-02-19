#define BOOST_ERROR_CODE_HEADER_ONLY

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

constexpr std::size_t kDefaultIterations = 250000;

struct bench_result {
    double total_ms{0.0};
    double avg_ns_per_poll{0.0};
    double polls_per_sec{0.0};
};

bool parse_iterations(int argc, char** argv, std::size_t& iterations) {
    if (argc <= 1) {
        return true;
    }

    std::size_t consumed = 0;
    const std::string_view arg{argv[1]};
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(std::string{arg}, &consumed, 10);
    } catch (...) {
        return false;
    }

    if (consumed != arg.size() || parsed == 0) {
        return false;
    }

    iterations = static_cast<std::size_t>(parsed);
    return true;
}

bench_result run_boost_asio_poll_wait(std::size_t iterations, int read_fd) {
    boost::asio::io_context io{};
    boost::asio::posix::stream_descriptor descriptor{io, read_fd};

    descriptor.async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        [](const boost::system::error_code&) {});

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        io.poll_one();
    }
    const auto end = std::chrono::steady_clock::now();

    boost::system::error_code ignored{};
    descriptor.cancel(ignored);
    io.restart();
    io.poll();

    const auto duration =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
    const double total_ms = duration.count() * 1000.0;
    const double total_s = duration.count();
    return bench_result{
        .total_ms = total_ms,
        .avg_ns_per_poll = (total_s * 1'000'000'000.0) /
                           static_cast<double>(iterations),
        .polls_per_sec = static_cast<double>(iterations) / total_s};
}

} // namespace

int main(int argc, char** argv) {
    std::size_t iterations = kDefaultIterations;
    if (!parse_iterations(argc, argv, iterations)) {
        std::cerr << "usage: perf_boost_asio_wait [iterations]\n";
        return 1;
    }

    std::array<int, 2> pipe_fds{};
    if (::pipe2(pipe_fds.data(), O_NONBLOCK | O_CLOEXEC) != 0) {
        std::cerr << "pipe2 failed\n";
        return 1;
    }

    const int read_fd = pipe_fds[0];
    const int write_fd = pipe_fds[1];

    const auto result = run_boost_asio_poll_wait(iterations, read_fd);
    if (result.total_ms <= 0.0) {
        std::cerr << "benchmark failed\n";
        ::close(read_fd);
        ::close(write_fd);
        return 1;
    }

    std::cout << "mode,iterations,total_ms,avg_ns_per_poll,polls_per_sec\n";
    std::cout << "boost_asio_poll_one_pending_wait," << iterations << ","
              << result.total_ms << "," << result.avg_ns_per_poll << ","
              << result.polls_per_sec << "\n";

    ::close(read_fd);
    ::close(write_fd);
    return 0;
}
