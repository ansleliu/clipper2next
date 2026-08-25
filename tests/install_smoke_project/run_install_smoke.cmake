cmake_minimum_required(VERSION 3.24)

foreach(required_var IN ITEMS
    CLIPPER2NEXT_BINARY_DIR
    CLIPPER2NEXT_INSTALL_PREFIX
    CLIPPER2NEXT_INSTALL_SMOKE_SOURCE_DIR
    CLIPPER2NEXT_SOURCE_DIR
    CLIPPER2NEXT_PYTHON_EXECUTABLE
    CLIPPER2NEXT_CXX_COMPILER
    CLIPPER2NEXT_INSTALL_SMOKE_BINARY_DIR
    CLIPPER2NEXT_CMAKE_GENERATOR)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "${required_var} is required")
  endif()
endforeach()

if(NOT DEFINED CMAKE_CTEST_COMMAND OR "${CMAKE_CTEST_COMMAND}" STREQUAL "")
  get_filename_component(cmake_binary_dir "${CMAKE_COMMAND}" DIRECTORY)
  find_program(CMAKE_CTEST_COMMAND NAMES ctest ctest.exe HINTS "${cmake_binary_dir}" REQUIRED)
endif()

file(REMOVE_RECURSE
  "${CLIPPER2NEXT_INSTALL_PREFIX}"
  "${CLIPPER2NEXT_INSTALL_SMOKE_BINARY_DIR}"
)

set(install_args
  --install "${CLIPPER2NEXT_BINARY_DIR}"
  --prefix "${CLIPPER2NEXT_INSTALL_PREFIX}"
)

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${install_args}
  RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "clipper2next install failed with exit code ${install_result}")
endif()

if(WIN32)
  file(GLOB installed_runtime_libraries
    "${CLIPPER2NEXT_INSTALL_PREFIX}/lib/*.dll")
  file(GLOB misplaced_runtime_libraries
    "${CLIPPER2NEXT_INSTALL_PREFIX}/bin/*.dll")
  if(NOT installed_runtime_libraries)
    message(FATAL_ERROR "Windows shared library was not installed under lib")
  endif()
  if(misplaced_runtime_libraries)
    message(FATAL_ERROR "Windows shared library must not be installed under bin")
  endif()
else()
  file(GLOB installed_runtime_libraries
    "${CLIPPER2NEXT_INSTALL_PREFIX}/lib/libclipper2next.so*")
  if(NOT installed_runtime_libraries)
    message(FATAL_ERROR "shared library was not installed under lib")
  endif()
endif()

execute_process(
  COMMAND
    "${CLIPPER2NEXT_PYTHON_EXECUTABLE}"
    "${CLIPPER2NEXT_SOURCE_DIR}/tools/checks/check_install_public_headers.py"
    --install-root "${CLIPPER2NEXT_INSTALL_PREFIX}"
  RESULT_VARIABLE header_audit_result
  OUTPUT_VARIABLE header_audit_output
  ERROR_VARIABLE header_audit_error
)
if(NOT header_audit_result EQUAL 0)
  message(FATAL_ERROR
    "installed header audit failed with exit code ${header_audit_result}:\n"
    "${header_audit_output}${header_audit_error}")
endif()

set(configure_args
  -S "${CLIPPER2NEXT_INSTALL_SMOKE_SOURCE_DIR}"
  -B "${CLIPPER2NEXT_INSTALL_SMOKE_BINARY_DIR}"
  -G "${CLIPPER2NEXT_CMAKE_GENERATOR}"
  "-DCMAKE_PREFIX_PATH=${CLIPPER2NEXT_INSTALL_PREFIX}"
  "-DCMAKE_CXX_COMPILER=${CLIPPER2NEXT_CXX_COMPILER}"
)
if(DEFINED CLIPPER2NEXT_CMAKE_TOOLCHAIN_FILE
   AND NOT "${CLIPPER2NEXT_CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND configure_args "-DCMAKE_TOOLCHAIN_FILE=${CLIPPER2NEXT_CMAKE_TOOLCHAIN_FILE}")
endif()
if(DEFINED CLIPPER2NEXT_CMAKE_BUILD_TYPE
   AND NOT "${CLIPPER2NEXT_CMAKE_BUILD_TYPE}" STREQUAL "")
  list(APPEND configure_args "-DCMAKE_BUILD_TYPE=${CLIPPER2NEXT_CMAKE_BUILD_TYPE}")
endif()
if(DEFINED CLIPPER2NEXT_VCPKG_INSTALLED_DIR
   AND NOT "${CLIPPER2NEXT_VCPKG_INSTALLED_DIR}" STREQUAL "")
  list(APPEND configure_args "-DVCPKG_INSTALLED_DIR=${CLIPPER2NEXT_VCPKG_INSTALLED_DIR}")
endif()
if(DEFINED CLIPPER2NEXT_VCPKG_TARGET_TRIPLET
   AND NOT "${CLIPPER2NEXT_VCPKG_TARGET_TRIPLET}" STREQUAL "")
  list(APPEND configure_args "-DVCPKG_TARGET_TRIPLET=${CLIPPER2NEXT_VCPKG_TARGET_TRIPLET}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${configure_args}
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "install smoke configure failed with exit code ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${CLIPPER2NEXT_INSTALL_SMOKE_BINARY_DIR}"
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "install smoke build failed with exit code ${build_result}")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}"
    --test-dir "${CLIPPER2NEXT_INSTALL_SMOKE_BINARY_DIR}"
    --output-on-failure
  RESULT_VARIABLE test_result
)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "install smoke test failed with exit code ${test_result}")
endif()
