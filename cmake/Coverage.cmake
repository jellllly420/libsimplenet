function(simplenet_enable_coverage target_name)
  if(NOT SIMPLENET_ENABLE_COVERAGE)
    return()
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(WARNING "Coverage requested, but compiler does not support gcov-style coverage flags.")
    return()
  endif()

  target_compile_options(${target_name} PUBLIC --coverage -O0 -g)
  target_link_options(${target_name} PUBLIC --coverage)
endfunction()

function(simplenet_add_coverage_target)
  if(NOT SIMPLENET_ENABLE_COVERAGE)
    return()
  endif()

  find_program(GCOVR_EXECUTABLE gcovr)
  if(NOT GCOVR_EXECUTABLE)
    message(FATAL_ERROR "SIMPLENET_ENABLE_COVERAGE=ON requires gcovr on PATH")
  endif()

  add_custom_target(
    coverage
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
    COMMAND
      ${GCOVR_EXECUTABLE}
      ${CMAKE_BINARY_DIR}
      --root ${CMAKE_SOURCE_DIR}
      --filter ${CMAKE_SOURCE_DIR}/src
      --merge-mode-functions=merge-use-line-max
      --exclude-unreachable-branches
      --print-summary
      --xml ${CMAKE_BINARY_DIR}/coverage/coverage.xml
      --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests and generating coverage reports in ${CMAKE_BINARY_DIR}/coverage"
    VERBATIM
  )
endfunction()
