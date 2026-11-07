cmake_minimum_required(VERSION 3.24)

project(clipper2next_package_test LANGUAGES CXX)

find_package(clipper2next CONFIG REQUIRED)

add_executable(clipper2next_package_test main.cpp correction_contract.cpp)
target_link_libraries(clipper2next_package_test
  PRIVATE
    clipper2next::geotypes
    clipper2next::clipper2next
    ${CMAKE_DL_LIBS}
)
target_compile_features(clipper2next_package_test PRIVATE cxx_std_23)
