function(clipper2next_target_warnings target_name)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    if(CLIPPER2NEXT_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    if(CLIPPER2NEXT_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(clipper2next_target_sanitizers target_name)
  if(NOT CLIPPER2NEXT_ENABLE_ASAN AND
      NOT CLIPPER2NEXT_ENABLE_UBSAN AND
      NOT CLIPPER2NEXT_ENABLE_TSAN)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if(CLIPPER2NEXT_ENABLE_UBSAN OR CLIPPER2NEXT_ENABLE_TSAN)
      message(FATAL_ERROR "UBSan and TSan gates require a GNU or Clang compiler frontend")
    endif()
    # An ASan-instrumented MSVC static library changes STL container annotation
    # metadata. Consumers must compile with the same setting or the linker
    # correctly rejects the mixed object graph (LNK2038).
    target_compile_options(${target_name} PUBLIC /fsanitize=address)
    target_compile_options(${target_name} PRIVATE /Zi)
    target_link_options(${target_name} PUBLIC /INCREMENTAL:NO /DEBUG)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(CLIPPER2NEXT_ENABLE_ASAN AND CLIPPER2NEXT_ENABLE_TSAN)
      message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled in the same target graph")
    endif()

    set(clipper2next_sanitizers "")
    if(CLIPPER2NEXT_ENABLE_ASAN)
      list(APPEND clipper2next_sanitizers "address")
    endif()
    if(CLIPPER2NEXT_ENABLE_UBSAN)
      list(APPEND clipper2next_sanitizers "undefined")
    endif()
    if(CLIPPER2NEXT_ENABLE_TSAN)
      list(APPEND clipper2next_sanitizers "thread")
    endif()

    list(JOIN clipper2next_sanitizers "," clipper2next_sanitize_arg)
    target_compile_options(
      ${target_name}
      PUBLIC
        -fsanitize=${clipper2next_sanitize_arg}
        -fno-omit-frame-pointer)
    target_link_options(
      ${target_name}
      PUBLIC
        -fsanitize=${clipper2next_sanitize_arg})

    if(CLIPPER2NEXT_ENABLE_UBSAN)
      target_compile_options(${target_name} PRIVATE -fno-sanitize-recover=undefined)
      target_link_options(${target_name} PUBLIC -fno-sanitize-recover=undefined)
    endif()
  endif()
endfunction()
