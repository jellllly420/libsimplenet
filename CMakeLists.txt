cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project(libsimplenet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SIMPLENET_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." ON)
option(SIMPLENET_BUILD_TESTS "Build tests." ON)
option(SIMPLENET_BUILD_EXAMPLES "Build examples." ON)
option(SIMPLENET_BUILD_DOCS "Enable Doxygen API documentation target." ON)
option(
  SIMPLENET_BUILD_BOOST_BENCHMARKS
  "Build Boost.Asio comparison benchmarks for perf suite."
  OFF
)
option(SIMPLENET_ENABLE_ASAN "Enable AddressSanitizer in Debug builds." ON)
option(SIMPLENET_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer in Debug builds." ON)
option(SIMPLENET_ENABLE_COVERAGE "Enable gcov/gcovr coverage instrumentation." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(Sanitizers)
include(Coverage)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_library(SIMPLENET_URING_LIBRARY NAMES uring REQUIRED)

function(simplenet_configure_target target_name)
  target_include_directories(
    ${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_compile_features(${target_name} PUBLIC cxx_std_23)
  simplenet_set_project_warnings(${target_name})
  simplenet_enable_sanitizers(${target_name})
  simplenet_enable_coverage(${target_name})
endfunction()

add_library(
  simplenet_core
  src/core/error.cpp
  src/core/unique_fd.cpp
)
add_library(simplenet::core ALIAS simplenet_core)
simplenet_configure_target(simplenet_core)
set_target_properties(simplenet_core PROPERTIES EXPORT_NAME core)

add_library(
  simplenet_blocking
  src/blocking/endpoint.cpp
  src/blocking/socket_helpers.cpp
  src/blocking/tcp.cpp
  src/blocking/udp.cpp
)
add_library(simplenet::blocking ALIAS simplenet_blocking)
target_link_libraries(simplenet_blocking PUBLIC simplenet::core)
simplenet_configure_target(simplenet_blocking)
set_target_properties(simplenet_blocking PROPERTIES EXPORT_NAME blocking)

add_library(
  simplenet_epoll
  src/epoll/reactor.cpp
)
add_library(simplenet::epoll ALIAS simplenet_epoll)
target_link_libraries(simplenet_epoll PUBLIC simplenet::core)
simplenet_configure_target(simplenet_epoll)
set_target_properties(simplenet_epoll PROPERTIES EXPORT_NAME epoll)

add_library(
  simplenet_uring
  src/uring/reactor.cpp
)
add_library(simplenet::uring ALIAS simplenet_uring)
target_link_libraries(
  simplenet_uring
  PUBLIC
    simplenet::core
    uring
)
simplenet_configure_target(simplenet_uring)
set_target_properties(simplenet_uring PROPERTIES EXPORT_NAME uring)

add_library(
  simplenet_runtime
  src/nonblocking/tcp.cpp
  src/runtime/engine.cpp
  src/runtime/event_loop.cpp
  src/runtime/io_ops.cpp
  src/runtime/resolver.cpp
  src/runtime/uring_event_loop.cpp
  src/runtime/write_queue.cpp
)
add_library(simplenet::runtime ALIAS simplenet_runtime)
target_link_libraries(
  simplenet_runtime
  PUBLIC
    simplenet::blocking
    simplenet::epoll
    simplenet::uring
)
simplenet_configure_target(simplenet_runtime)
set_target_properties(simplenet_runtime PROPERTIES EXPORT_NAME runtime)

add_library(simplenet INTERFACE)
add_library(simplenet::simplenet ALIAS simplenet)
target_link_libraries(
  simplenet
  INTERFACE
    simplenet::blocking
    simplenet::runtime
)
target_include_directories(
  simplenet
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(simplenet PROPERTIES EXPORT_NAME simplenet)

install(
  TARGETS
    simplenet_core
    simplenet_blocking
    simplenet_epoll
    simplenet_uring
    simplenet_runtime
    simplenet
  EXPORT simplenetTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT simplenetTargets
  FILE libsimplenetTargets.cmake
  NAMESPACE simplenet::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsimplenet
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libsimplenetConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/libsimplenetConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsimplenet
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/libsimplenetConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libsimplenetConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libsimplenetConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsimplenet
)

if(SIMPLENET_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  simplenet_add_coverage_target()
endif()

if(SIMPLENET_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(SIMPLENET_BUILD_DOCS)
  find_package(Doxygen)
  if(DOXYGEN_FOUND)
    set(SIMPLENET_DOXYGEN_OUTPUT_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen")
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
      "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
      @ONLY
    )
    add_custom_target(
      doc
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${SIMPLENET_DOXYGEN_OUTPUT_DIR}"
      COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM
    )
  else()
    message(STATUS "Doxygen not found; 'doc' target will be unavailable.")
  endif()
endif()
