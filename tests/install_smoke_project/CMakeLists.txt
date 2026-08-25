cmake_minimum_required(VERSION 3.24)

project(clipper2next_install_smoke LANGUAGES CXX)

find_package(clipper2next CONFIG REQUIRED)

if(NOT TARGET clipper2next::geotypes)
  message(FATAL_ERROR "clipper2next package did not export clipper2next::geotypes")
endif()

add_executable(clipper2next_install_smoke main.cpp)
target_link_libraries(clipper2next_install_smoke
  PRIVATE
    clipper2next::geotypes
    clipper2next::clipper2next
)
target_compile_features(clipper2next_install_smoke PRIVATE cxx_std_23)

enable_testing()
add_test(NAME clipper2next_install_smoke COMMAND clipper2next_install_smoke)
if(WIN32)
  set_tests_properties(clipper2next_install_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:clipper2next::clipper2next>"
  )
endif()
