include(FetchContent)
include(GoogleTest)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE
)
FetchContent_MakeAvailable(googletest)

function(simplenet_add_test_target)
  cmake_parse_arguments(
    SNETTEST
    ""
    "NAME"
    "SOURCES;LIBS;LABELS"
    ${ARGN}
  )

  if(NOT SNETTEST_NAME)
    message(FATAL_ERROR "simplenet_add_test_target requires NAME")
  endif()

  add_executable(${SNETTEST_NAME} ${SNETTEST_SOURCES})
  target_link_libraries(
    ${SNETTEST_NAME}
    PRIVATE
      ${SNETTEST_LIBS}
      GTest::gtest_main
  )

  target_compile_features(${SNETTEST_NAME} PRIVATE cxx_std_23)
  simplenet_set_project_warnings(${SNETTEST_NAME})
  simplenet_enable_sanitizers(${SNETTEST_NAME})
  simplenet_enable_coverage(${SNETTEST_NAME})

  gtest_discover_tests(
    ${SNETTEST_NAME}
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "${SNETTEST_LABELS}"
  )
endfunction()

simplenet_add_test_target(
  NAME simplenet_test_core_unit
  SOURCES
    unit/test_error.cpp
    unit/test_result.cpp
    unit/test_unique_fd.cpp
  LIBS simplenet::core
  LABELS core;unit
)

simplenet_add_test_target(
  NAME simplenet_test_blocking
  SOURCES
    integration/test_blocking_tcp.cpp
    integration/test_blocking_udp.cpp
  LIBS simplenet::blocking
  LABELS foundation;integration;blocking
)

simplenet_add_test_target(
  NAME simplenet_test_epoll
  SOURCES integration/test_epoll_reactor.cpp
  LIBS
    simplenet::blocking
    simplenet::epoll
    simplenet::runtime
  LABELS foundation;integration;epoll
)

simplenet_add_test_target(
  NAME simplenet_test_runtime_coroutines
  SOURCES integration/test_runtime_coroutines.cpp
  LIBS
    simplenet::blocking
    simplenet::runtime
  LABELS foundation;integration;runtime
)

simplenet_add_test_target(
  NAME simplenet_test_runtime_timers
  SOURCES integration/test_runtime_timers.cpp
  LIBS
    simplenet::blocking
    simplenet::runtime
  LABELS foundation;integration;runtime;timers
)

simplenet_add_test_target(
  NAME simplenet_test_runtime_backpressure
  SOURCES integration/test_runtime_backpressure.cpp
  LIBS
    simplenet::blocking
    simplenet::runtime
  LABELS foundation;integration;runtime;backpressure
)

simplenet_add_test_target(
  NAME simplenet_test_runtime_resolver
  SOURCES integration/test_runtime_resolver.cpp
  LIBS simplenet::runtime
  LABELS foundation;integration;runtime;resolver
)

simplenet_add_test_target(
  NAME simplenet_test_runtime_uring
  SOURCES
    integration/test_runtime_uring.cpp
    integration/test_uring_reactor.cpp
  LIBS
    simplenet::blocking
    simplenet::runtime
    simplenet::uring
  LABELS foundation;integration;uring
)

simplenet_add_test_target(
  NAME simplenet_test_runtime_engine
  SOURCES integration/test_runtime_engine.cpp
  LIBS simplenet::runtime
  LABELS foundation;integration;runtime;backend
)
