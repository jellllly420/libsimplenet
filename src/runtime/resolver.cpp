#include "simplenet/runtime/resolver.hpp"

#include "simplenet/runtime/io_ops.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct resolve_state {
    std::mutex mutex{};
    bool ready{false};
    std::atomic_bool canceled{false};
    std::optional<simplenet::result<std::vector<simplenet::runtime::endpoint>>>
        result{};
};

simplenet::result<std::vector<simplenet::runtime::endpoint>>
resolve_ipv4_tcp_endpoints(const std::string& host,
                           const std::string& service) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* raw_result = nullptr;
    const int resolve_status =
        ::getaddrinfo(host.c_str(), service.c_str(), &hints, &raw_result);

    if (resolve_status != 0) {
        int mapped = EHOSTUNREACH;
        if      (resolve_status == EAI_AGAIN)  { mapped = EAGAIN; }
        else if (resolve_status == EAI_NONAME) { mapped = ENOENT; }
        else if (resolve_status == EAI_MEMORY) { mapped = ENOMEM; }
        return simplenet::err<std::vector<simplenet::runtime::endpoint>>(
            simplenet::make_error_from_errno(mapped));
    }

    std::vector<simplenet::runtime::endpoint> endpoints;
    for (addrinfo* cursor = raw_result; cursor != nullptr;
         cursor = cursor->ai_next) {
        if (cursor->ai_family != AF_INET || cursor->ai_addr == nullptr) {
            continue;
        }

        const auto* ipv4 =
            reinterpret_cast<const sockaddr_in*>(cursor->ai_addr);
        char host_buffer[INET_ADDRSTRLEN] = {};
        const char* converted = ::inet_ntop(AF_INET, &ipv4->sin_addr,
                                            host_buffer,
                                            sizeof(host_buffer));
        if (converted == nullptr) {
            continue;
        }

        endpoints.push_back(simplenet::runtime::endpoint{
            std::string{host_buffer}, ntohs(ipv4->sin_port)});
    }

    ::freeaddrinfo(raw_result);

    if (endpoints.empty()) {
        return simplenet::err<std::vector<simplenet::runtime::endpoint>>(
            simplenet::make_error_from_errno(ENOENT));
    }
    return endpoints;
}

class resolver_worker final {
public:
    static resolver_worker& instance() {
        static resolver_worker worker;
        return worker;
    }

    void enqueue(std::string host, std::string service,
                 std::shared_ptr<resolve_state> state) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push_back(
                job{std::move(host), std::move(service), std::move(state)});
        }
        cv_.notify_one();
    }

private:
    struct job {
        std::string host;
        std::string service;
        std::shared_ptr<resolve_state> state;
    };

    resolver_worker() : thread_([this]() { run(); }) {}

    ~resolver_worker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    resolver_worker(const resolver_worker&) = delete;
    resolver_worker& operator=(const resolver_worker&) = delete;

    void run() {
        while (true) {
            job next{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stop_ || !jobs_.empty(); });

                if (stop_ && jobs_.empty()) {
                    return;
                }

                next = std::move(jobs_.front());
                jobs_.pop_front();
            }

            if (next.state->canceled.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(next.state->mutex);
                next.state->result = simplenet::err<std::vector<simplenet::runtime::endpoint>>(
                    simplenet::make_error_from_errno(ECANCELED));
                next.state->ready = true;
                continue;
            }

            auto resolved =
                resolve_ipv4_tcp_endpoints(next.host, next.service);
            std::lock_guard<std::mutex> lock(next.state->mutex);
            next.state->result = std::move(resolved);
            next.state->ready = true;
        }
    }

    std::mutex mutex_{};
    std::condition_variable cv_{};
    std::deque<job> jobs_{};
    bool stop_{false};
    std::thread thread_{};
};

} // namespace

namespace simplenet::runtime {

result<endpoint> parse_ipv4_endpoint(std::string_view value) {
    const std::size_t separator = value.rfind(':');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= value.size()) {
        return err<endpoint>(make_error_from_errno(EINVAL));
    }

    const std::string host{value.substr(0, separator)};
    const std::string port_text{value.substr(separator + 1)};

    std::uint32_t port = 0;
    for (const char ch : port_text) {
        if (ch < '0' || ch > '9') {
            return err<endpoint>(make_error_from_errno(EINVAL));
        }
        port = port * 10U + static_cast<std::uint32_t>(ch - '0');
        if (port > 65535U) {
            return err<endpoint>(make_error_from_errno(EINVAL));
        }
    }

    in_addr parsed{};
    if (::inet_pton(AF_INET, host.c_str(), &parsed) != 1) {
        return err<endpoint>(make_error_from_errno(EINVAL));
    }

    return endpoint{host, static_cast<std::uint16_t>(port)};
}

std::string format_endpoint(const endpoint& value) {
    return value.host + ":" + std::to_string(value.port);
}

task<result<std::vector<endpoint>>>
async_resolve(std::string host, std::string service, cancel_token token) {
    if (token.stop_requested()) {
        co_return err<std::vector<endpoint>>(make_error_from_errno(ECANCELED));
    }

    auto state = std::make_shared<resolve_state>();
    resolver_worker::instance().enqueue(std::move(host), std::move(service),
                                        state);

    while (true) {
        if (token.stop_requested()) {
            state->canceled.store(true, std::memory_order_release);
            co_return err<std::vector<endpoint>>(
                make_error_from_errno(ECANCELED));
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->ready) {
                co_return std::move(state->result.value());
            }
        }

        const auto sleep_result = co_await async_sleep(10ms, token);
        if (!sleep_result.has_value()) {
            co_return err<std::vector<endpoint>>(sleep_result.error());
        }
    }
}

} // namespace simplenet::runtime
