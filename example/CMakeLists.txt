add_executable(simplenet_echo_server echo_server.cpp)
target_link_libraries(simplenet_echo_server PRIVATE simplenet::simplenet)
simplenet_set_project_warnings(simplenet_echo_server)
simplenet_enable_sanitizers(simplenet_echo_server)
simplenet_enable_coverage(simplenet_echo_server)

add_executable(simplenet_echo_client echo_client.cpp)
target_link_libraries(simplenet_echo_client PRIVATE simplenet::simplenet)
simplenet_set_project_warnings(simplenet_echo_client)
simplenet_enable_sanitizers(simplenet_echo_client)
simplenet_enable_coverage(simplenet_echo_client)

add_executable(simplenet_backend_switch backend_switch.cpp)
target_link_libraries(simplenet_backend_switch PRIVATE simplenet::simplenet)
simplenet_set_project_warnings(simplenet_backend_switch)
simplenet_enable_sanitizers(simplenet_backend_switch)
simplenet_enable_coverage(simplenet_backend_switch)

add_executable(simplenet_perf_reactor_wait perf_reactor_wait.cpp)
target_link_libraries(simplenet_perf_reactor_wait PRIVATE simplenet::simplenet)
simplenet_set_project_warnings(simplenet_perf_reactor_wait)
simplenet_enable_sanitizers(simplenet_perf_reactor_wait)
simplenet_enable_coverage(simplenet_perf_reactor_wait)

add_executable(
  simplenet_perf_tcp_echo_libsimplenet
  perf_tcp_echo_libsimplenet.cpp
)
target_link_libraries(
  simplenet_perf_tcp_echo_libsimplenet
  PRIVATE
    simplenet::simplenet
)
simplenet_set_project_warnings(simplenet_perf_tcp_echo_libsimplenet)
simplenet_enable_sanitizers(simplenet_perf_tcp_echo_libsimplenet)
simplenet_enable_coverage(simplenet_perf_tcp_echo_libsimplenet)

add_executable(
  simplenet_perf_connection_churn_libsimplenet
  perf_connection_churn_libsimplenet.cpp
)
target_link_libraries(
  simplenet_perf_connection_churn_libsimplenet
  PRIVATE
    simplenet::simplenet
)
simplenet_set_project_warnings(simplenet_perf_connection_churn_libsimplenet)
simplenet_enable_sanitizers(simplenet_perf_connection_churn_libsimplenet)
simplenet_enable_coverage(simplenet_perf_connection_churn_libsimplenet)

add_executable(
  simplenet_perf_async_echo_libsimplenet
  perf_async_echo_libsimplenet.cpp
)
target_link_libraries(
  simplenet_perf_async_echo_libsimplenet
  PRIVATE
    simplenet::simplenet
)
simplenet_set_project_warnings(simplenet_perf_async_echo_libsimplenet)
simplenet_enable_sanitizers(simplenet_perf_async_echo_libsimplenet)
simplenet_enable_coverage(simplenet_perf_async_echo_libsimplenet)

if(SIMPLENET_BUILD_BOOST_BENCHMARKS)
  find_package(Boost REQUIRED COMPONENTS system)
  find_package(Threads REQUIRED)

  add_executable(simplenet_perf_boost_asio_wait perf_boost_asio_wait.cpp)
  target_link_libraries(
    simplenet_perf_boost_asio_wait
    PRIVATE
      Boost::system
      Threads::Threads
  )
  simplenet_set_project_warnings(simplenet_perf_boost_asio_wait)
  simplenet_enable_sanitizers(simplenet_perf_boost_asio_wait)
  simplenet_enable_coverage(simplenet_perf_boost_asio_wait)

  add_executable(simplenet_perf_tcp_echo_boost_asio perf_tcp_echo_boost_asio.cpp)
  target_link_libraries(
    simplenet_perf_tcp_echo_boost_asio
    PRIVATE
      Boost::system
      Threads::Threads
  )
  simplenet_set_project_warnings(simplenet_perf_tcp_echo_boost_asio)
  simplenet_enable_sanitizers(simplenet_perf_tcp_echo_boost_asio)
  simplenet_enable_coverage(simplenet_perf_tcp_echo_boost_asio)

  add_executable(
    simplenet_perf_connection_churn_boost_asio
    perf_connection_churn_boost_asio.cpp
  )
  target_link_libraries(
    simplenet_perf_connection_churn_boost_asio
    PRIVATE
      Boost::system
      Threads::Threads
  )
  simplenet_set_project_warnings(simplenet_perf_connection_churn_boost_asio)
  simplenet_enable_sanitizers(simplenet_perf_connection_churn_boost_asio)
  simplenet_enable_coverage(simplenet_perf_connection_churn_boost_asio)

  add_executable(simplenet_perf_async_echo_boost_asio perf_async_echo_boost_asio.cpp)
  target_link_libraries(
    simplenet_perf_async_echo_boost_asio
    PRIVATE
      Boost::system
      Threads::Threads
  )
  simplenet_set_project_warnings(simplenet_perf_async_echo_boost_asio)
  simplenet_enable_sanitizers(simplenet_perf_async_echo_boost_asio)
  simplenet_enable_coverage(simplenet_perf_async_echo_boost_asio)
endif()

if(SIMPLENET_BUILD_TESTS)
  set(
    SIMPLENET_ASSERT_FAILING_COMMAND
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests/scripts/assert_failing_command.sh"
  )

  add_test(
    NAME simplenet_example_backend_switch_epoll
    COMMAND simplenet_backend_switch epoll
  )

  add_test(
    NAME simplenet_example_backend_switch_invalid_backend
    COMMAND
      /bin/sh
      "${SIMPLENET_ASSERT_FAILING_COMMAND}"
      "unknown backend"
      "$<TARGET_FILE:simplenet_backend_switch>"
      "invalid"
  )
  set_tests_properties(
    simplenet_example_backend_switch_invalid_backend
    PROPERTIES
      LABELS "example;smoke"
  )

  add_test(
    NAME simplenet_example_backend_switch_extra_args
    COMMAND
      /bin/sh
      "${SIMPLENET_ASSERT_FAILING_COMMAND}"
      "usage: simplenet_backend_switch"
      "$<TARGET_FILE:simplenet_backend_switch>"
      "epoll"
      "extra"
  )
  set_tests_properties(
    simplenet_example_backend_switch_extra_args
    PROPERTIES
      LABELS "example;smoke"
  )

  set_tests_properties(
    simplenet_example_backend_switch_epoll
    PROPERTIES LABELS "example;smoke"
  )

  add_test(
    NAME simplenet_example_echo_server_invalid_arg_count
    COMMAND
      /bin/sh
      "${SIMPLENET_ASSERT_FAILING_COMMAND}"
      "usage: simplenet_echo_server"
      "$<TARGET_FILE:simplenet_echo_server>"
      "8080"
      "extra"
  )
  set_tests_properties(
    simplenet_example_echo_server_invalid_arg_count
    PROPERTIES
      LABELS "example;smoke"
  )

  add_test(
    NAME simplenet_example_echo_server_invalid_port
    COMMAND
      /bin/sh
      "${SIMPLENET_ASSERT_FAILING_COMMAND}"
      "invalid port argument|port must be in range"
      "$<TARGET_FILE:simplenet_echo_server>"
      "8080abc"
  )
  set_tests_properties(
    simplenet_example_echo_server_invalid_port
    PROPERTIES
      LABELS "example;smoke"
  )

  add_test(
    NAME simplenet_example_echo_client_invalid_arg_count
    COMMAND
      /bin/sh
      "${SIMPLENET_ASSERT_FAILING_COMMAND}"
      "usage: simplenet_echo_client"
      "$<TARGET_FILE:simplenet_echo_client>"
      "127.0.0.1"
      "8080"
      "ping"
      "extra"
  )
  set_tests_properties(
    simplenet_example_echo_client_invalid_arg_count
    PROPERTIES
      LABELS "example;smoke"
  )

  add_test(
    NAME simplenet_example_echo_client_invalid_port
    COMMAND
      /bin/sh
      "${SIMPLENET_ASSERT_FAILING_COMMAND}"
      "invalid port argument|port must be in range"
      "$<TARGET_FILE:simplenet_echo_client>"
      "127.0.0.1"
      "8080abc"
  )
  set_tests_properties(
    simplenet_example_echo_client_invalid_port
    PROPERTIES
      LABELS "example;smoke"
  )
endif()
