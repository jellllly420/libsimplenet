#include "simplenet/epoll/reactor.hpp"
#include "simplenet/core/unique_fd.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::size_t kDefaultIterations = 200000;
constexpr std::size_t kBatchSize = 64;

enum class bench_mode {
    all,
    alloc_only,
    reuse_only,
};

struct bench_result {
    double total_ms{0.0};
    double avg_ns_per_wait{0.0};
    double waits_per_sec{0.0};
};

bench_result run_alloc_baseline(int epoll_fd, std::size_t iterations) {
    std::array<simplenet::epoll::ready_event, kBatchSize> output{};
    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < iterations; ++i) {
        std::vector<::epoll_event> sys_events(output.size());
        const int ready = ::epoll_wait(epoll_fd, sys_events.data(),
                                       static_cast<int>(sys_events.size()), 0);
        if (ready > 0) {
            for (int j = 0; j < ready; ++j) {
                output[static_cast<std::size_t>(j)] = simplenet::epoll::ready_event{
                    .fd = sys_events[static_cast<std::size_t>(j)].data.fd,
                    .events = sys_events[static_cast<std::size_t>(j)].events};
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
    const double total_ms = duration.count() * 1000.0;
    const double total_s = duration.count();
    return bench_result{
        .total_ms = total_ms,
        .avg_ns_per_wait = (total_s * 1'000'000'000.0) /
                           static_cast<double>(iterations),
        .waits_per_sec = static_cast<double>(iterations) / total_s};
}

bench_result run_reuse_path(simplenet::epoll::reactor& reactor,
                            std::size_t iterations) {
    std::array<simplenet::epoll::ready_event, kBatchSize> events{};
    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < iterations; ++i) {
        auto wait_result = reactor.wait(events, std::chrono::milliseconds{0});
        if (!wait_result.has_value()) {
            return {};
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
    const double total_ms = duration.count() * 1000.0;
    const double total_s = duration.count();
    return bench_result{
        .total_ms = total_ms,
        .avg_ns_per_wait = (total_s * 1'000'000'000.0) /
                           static_cast<double>(iterations),
        .waits_per_sec = static_cast<double>(iterations) / total_s};
}

bool parse_mode(const std::string_view value, bench_mode& mode) {
    if (value == "all") {
        mode = bench_mode::all;
        return true;
    }
    if (value == "alloc") {
        mode = bench_mode::alloc_only;
        return true;
    }
    if (value == "reuse") {
        mode = bench_mode::reuse_only;
        return true;
    }
    return false;
}

bool parse_args(int argc, char** argv, std::size_t& iterations, bench_mode& mode) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--mode") {
            if (i + 1 >= argc) {
                return false;
            }
            if (!parse_mode(std::string_view{argv[++i]}, mode)) {
                return false;
            }
            continue;
        }

        std::size_t consumed = 0;
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
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::size_t iterations = kDefaultIterations;
    bench_mode mode = bench_mode::all;
    if (!parse_args(argc, argv, iterations, mode)) {
        std::cerr
            << "usage: simplenet_perf_reactor_wait [iterations] [--mode all|alloc|reuse]\n";
        return 1;
    }

    std::array<int, 2> pipe_fds{};
    if (::pipe2(pipe_fds.data(), O_NONBLOCK | O_CLOEXEC) != 0) {
        std::cerr << "pipe2 failed\n";
        return 1;
    }

    simplenet::unique_fd read_end{pipe_fds[0]};
    simplenet::unique_fd write_end{pipe_fds[1]};

    auto reactor_result = simplenet::epoll::reactor::create();
    if (!reactor_result.has_value()) {
        std::cerr << "reactor create failed: " << reactor_result.error().message()
                  << "\n";
        return 1;
    }
    auto reactor = std::move(reactor_result.value());

    const auto add_result = reactor.add(read_end.get(), EPOLLIN | EPOLLET);
    if (!add_result.has_value()) {
        std::cerr << "reactor add failed: " << add_result.error().message()
                  << "\n";
        return 1;
    }

    std::cout << "mode,iterations,total_ms,avg_ns_per_wait,waits_per_sec\n";

    if (mode == bench_mode::alloc_only) {
        const auto alloc = run_alloc_baseline(reactor.native_handle(), iterations);
        if (alloc.total_ms <= 0.0) {
            std::cerr << "benchmark failed\n";
            return 1;
        }
        std::cout << "alloc_baseline," << iterations << "," << alloc.total_ms << ","
                  << alloc.avg_ns_per_wait << "," << alloc.waits_per_sec << "\n";
        return 0;
    }

    if (mode == bench_mode::reuse_only) {
        const auto reuse = run_reuse_path(reactor, iterations);
        if (reuse.total_ms <= 0.0) {
            std::cerr << "benchmark failed\n";
            return 1;
        }
        std::cout << "reuse_path," << iterations << "," << reuse.total_ms << ","
                  << reuse.avg_ns_per_wait << "," << reuse.waits_per_sec << "\n";
        return 0;
    }

    const auto alloc = run_alloc_baseline(reactor.native_handle(), iterations);
    const auto reuse = run_reuse_path(reactor, iterations);
    if (reuse.total_ms <= 0.0 || alloc.total_ms <= 0.0) {
        std::cerr << "benchmark failed\n";
        return 1;
    }
    const double speedup = alloc.total_ms / reuse.total_ms;
    std::cout << "alloc_baseline," << iterations << "," << alloc.total_ms << ","
              << alloc.avg_ns_per_wait << "," << alloc.waits_per_sec << "\n";
    std::cout << "reuse_path," << iterations << "," << reuse.total_ms << ","
              << reuse.avg_ns_per_wait << "," << reuse.waits_per_sec << "\n";
    std::cout << "speedup_x," << speedup << "\n";
    return 0;
}
