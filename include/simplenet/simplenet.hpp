#pragma once

/**
 * @file
 * @brief Umbrella include for the complete libsimplenet public API.
 */

#include "simplenet/blocking/endpoint.hpp"
#include "simplenet/blocking/tcp.hpp"
#include "simplenet/blocking/udp.hpp"
#include "simplenet/core/error.hpp"
#include "simplenet/core/result.hpp"
#include "simplenet/core/unique_fd.hpp"
#include "simplenet/epoll/reactor.hpp"
#include "simplenet/io_context.hpp"
#include "simplenet/ip_tcp.hpp"
#include "simplenet/nonblocking/tcp.hpp"
#include "simplenet/runtime/cancel.hpp"
#include "simplenet/runtime/engine.hpp"
#include "simplenet/runtime/event_loop.hpp"
#include "simplenet/runtime/io_ops.hpp"
#include "simplenet/runtime/resolver.hpp"
#include "simplenet/runtime/task.hpp"
#include "simplenet/runtime/uring_event_loop.hpp"
#include "simplenet/runtime/write_queue.hpp"
#include "simplenet/uring/reactor.hpp"
