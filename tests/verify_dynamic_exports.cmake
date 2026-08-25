if(NOT DEFINED LIBRARY_FILE OR NOT EXISTS "${LIBRARY_FILE}")
  message(FATAL_ERROR "clipper2next shared library was not provided")
endif()
if(NOT DEFINED EXPORT_INSPECTOR OR NOT EXISTS "${EXPORT_INSPECTOR}")
  message(FATAL_ERROR "shared-library export inspector was not found")
endif()

if(WIN32)
  execute_process(
    COMMAND "${EXPORT_INSPECTOR}" /nologo /exports "${LIBRARY_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE exports
    ERROR_VARIABLE errors
  )
else()
  execute_process(
    COMMAND "${EXPORT_INSPECTOR}" -D --defined-only --demangle "${LIBRARY_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE exports
    ERROR_VARIABLE errors
  )
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "export inspection failed: ${errors}")
endif()

foreach(required_symbol IN ITEMS clip offset rect_clip triangulate)
  if(NOT exports MATCHES "${required_symbol}")
    message(FATAL_ERROR
      "clipper2next export table is missing public API ${required_symbol}")
  endif()
endforeach()

string(REPLACE "\n" ";" export_lines "${exports}")
foreach(line IN LISTS export_lines)
  if(WIN32)
    if(line MATCHES
       "^[ \t]*[0-9]+[ \t]+[0-9A-F]+[ \t]+[0-9A-F]+[ \t]+\\?")
      if(NOT line MATCHES "(@clipper2next@@|clipper2next::)")
        message(FATAL_ERROR
          "clipper2next exports a symbol outside its public namespace: ${line}")
      endif()
    endif()
  elseif(line MATCHES "^[0-9A-Fa-f]+[ \t]+[A-Za-z][ \t]+(.+)$")
    set(symbol "${CMAKE_MATCH_1}")
    if(NOT symbol MATCHES "^clipper2next::")
      message(FATAL_ERROR
        "clipper2next exports a symbol outside its public namespace: ${symbol}")
    endif()
  endif()
endforeach()

foreach(forbidden_pattern IN ITEMS
    "::internal::"
    "@internal@"
    "::detail::"
    "@detail@")
  if(exports MATCHES "${forbidden_pattern}")
    message(FATAL_ERROR
      "clipper2next exports private implementation symbol matching ${forbidden_pattern}")
  endif()
endforeach()
