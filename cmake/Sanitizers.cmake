function(simplenet_enable_sanitizers target_name)
  if(MSVC)
    return()
  endif()

  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    return()
  endif()

  set(sanitizer_flags "")

  if(SIMPLENET_ENABLE_ASAN)
    list(APPEND sanitizer_flags -fsanitize=address)
  endif()

  if(SIMPLENET_ENABLE_UBSAN)
    list(APPEND sanitizer_flags -fsanitize=undefined)
  endif()

  if(sanitizer_flags)
    target_compile_options(
      ${target_name}
      PUBLIC
        ${sanitizer_flags}
        -fno-omit-frame-pointer
    )
    target_link_options(${target_name} PUBLIC ${sanitizer_flags})
  endif()
endfunction()
