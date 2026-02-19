#pragma once

/**
 * @file
 * @brief Endpoint parsing/formatting and asynchronous DNS resolution.
 */

#include "simplenet/blocking/endpoint.hpp"
#include "simplenet/runtime/cancel.hpp"
#include "simplenet/runtime/task.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace simplenet::runtime {

/// Alias to endpoint type used throughout runtime APIs.
using endpoint = simplenet::blocking::endpoint;

/**
 * @brief Parse `host:port` style IPv4 endpoint text.
 * @param value Input text.
 * @return Parsed endpoint on success.
 */
[[nodiscard]] result<endpoint> parse_ipv4_endpoint(std::string_view value);
/**
 * @brief Format an endpoint into `host:port`.
 * @param value Endpoint to format.
 */
[[nodiscard]] std::string format_endpoint(const endpoint& value);

/**
 * @brief Resolve host/service into a list of endpoints asynchronously.
 * @param host Hostname or literal address.
 * @param service Service name or port string.
 * @param token Optional cancellation token.
 */
[[nodiscard]] task<result<std::vector<endpoint>>>
async_resolve(std::string host, std::string service, cancel_token token = {});

} // namespace simplenet::runtime
