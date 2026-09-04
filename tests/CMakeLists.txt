include(FetchContent)
include(GoogleTest)

if(TARGET gtest AND TARGET gtest_main)
  set(clipper2next_gtest_libs gtest gtest_main)
elseif(TARGET GTest::gtest AND TARGET GTest::gtest_main)
  set(clipper2next_gtest_libs GTest::gtest GTest::gtest_main)
elseif(CLIPPER2NEXT_FETCH_DEPS)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  set(clipper2next_gtest_libs gtest gtest_main)
else()
  find_package(GTest REQUIRED)
  set(clipper2next_gtest_libs GTest::gtest GTest::gtest_main)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(clipper2next_gate_output_dir "${PROJECT_BINARY_DIR}/gates")
file(MAKE_DIRECTORY "${clipper2next_gate_output_dir}")

function(clipper2next_add_python_gate gate_name script)
  add_test(NAME clipper2next_gate_${gate_name}
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/${script}" ${ARGN})
  set_tests_properties(clipper2next_gate_${gate_name} PROPERTIES
    LABELS "gate;architecture")
endfunction()

clipper2next_add_python_gate(public_api_surface
  tools/checks/check_public_api_surface.py
  --root "${PROJECT_SOURCE_DIR}")
clipper2next_add_python_gate(concurrency_ownership
  tools/checks/check_concurrency_ownership.py
  --root "${PROJECT_SOURCE_DIR}")
clipper2next_add_python_gate(release_metadata
  tools/checks/check_release_metadata.py
  --root "${PROJECT_SOURCE_DIR}")
clipper2next_add_python_gate(public_release_information
  tools/checks/check_public_release_information.py
  --root "${PROJECT_SOURCE_DIR}"
  --output "${clipper2next_gate_output_dir}/public_release_information.txt")
clipper2next_add_python_gate(public_header_shape
  tools/checks/check_public_header_shape.py
  --root "${PROJECT_SOURCE_DIR}/include/clipper2next"
  --output "${clipper2next_gate_output_dir}/public_header_shape.txt")
clipper2next_add_python_gate(api_style
  tools/checks/check_api_style_shape.py
  --root "${PROJECT_SOURCE_DIR}/include/clipper2next"
  --output "${clipper2next_gate_output_dir}/api_style.txt"
  --strict-product)
clipper2next_add_python_gate(pascal_api
  tools/checks/check_product_pascal_api_shape.py
  --root "${PROJECT_SOURCE_DIR}/include/clipper2next"
  --output "${clipper2next_gate_output_dir}/pascal_api.txt"
  --strict-product)
clipper2next_add_python_gate(public_value_objects
  tools/checks/check_public_value_object_shape.py
  --root "${PROJECT_SOURCE_DIR}/include/clipper2next"
  --output "${clipper2next_gate_output_dir}/public_value_objects.txt"
  --strict-product)
clipper2next_add_python_gate(public_implementation_weight
  tools/checks/check_public_implementation_weight.py
  --root "${PROJECT_SOURCE_DIR}/include/clipper2next"
  --output "${clipper2next_gate_output_dir}/public_implementation_weight.txt"
  --strict)
clipper2next_add_python_gate(product_error_shape
  tools/checks/check_product_error_shape.py
  --root "${PROJECT_SOURCE_DIR}"
  --output "${clipper2next_gate_output_dir}/product_error_shape.txt"
  --strict-product)
clipper2next_add_python_gate(nullable_reference_shape
  tools/checks/check_persistent_nullable_ref_shape.py
  --root "${PROJECT_SOURCE_DIR}"
  --output "${clipper2next_gate_output_dir}/nullable_reference_shape.txt"
  --strict-product)
clipper2next_add_python_gate(complexity_shape
  tools/checks/check_complexity_shape.py
  --root "${PROJECT_SOURCE_DIR}"
  --output "${clipper2next_gate_output_dir}/complexity_shape.txt"
  --strict-product)
clipper2next_add_python_gate(compat_boundary
  tools/checks/check_product_compat_boundary.py
  --root "${PROJECT_SOURCE_DIR}"
  --cmake "${PROJECT_SOURCE_DIR}/CMakeLists.txt"
  --output "${clipper2next_gate_output_dir}/compat_boundary.txt"
  --strict-product)
clipper2next_add_python_gate(default_oracle_dependency
  tools/checks/check_default_oracle_dependency.py
  --root "${PROJECT_SOURCE_DIR}"
  --output "${clipper2next_gate_output_dir}/default_oracle_dependency.txt")
clipper2next_add_python_gate(oracle_vcpkg_dependency
  tools/checks/check_oracle_vcpkg_dependency.py
  --root "${PROJECT_SOURCE_DIR}"
  --output "${clipper2next_gate_output_dir}/oracle_vcpkg_dependency.txt")
clipper2next_add_python_gate(strict_oracle_equivalence
  tools/checks/check_strict_oracle_equivalence_shape.py
  --root "${PROJECT_SOURCE_DIR}"
  --output "${clipper2next_gate_output_dir}/strict_oracle_equivalence.txt")
clipper2next_add_python_gate(external_legacy_oracles
  tools/checks/check_external_verification_legacy_oracles.py
  --root "${PROJECT_SOURCE_DIR}")
clipper2next_add_python_gate(vendored_geometry_profiles
  tools/checks/check_geometry_corpus_profiles.py
  "${PROJECT_SOURCE_DIR}/tests/oracle/corpus/geometry")

add_test(NAME clipper2next_gate_conan_publication_contract
  COMMAND "${Python3_EXECUTABLE}" -m unittest
    tools.tests.build_install_tests
    tools.tests.check_release_metadata_tests)
set_tests_properties(clipper2next_gate_conan_publication_contract PROPERTIES
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  LABELS "gate;architecture;packaging")

add_test(NAME clipper2next_gate_evidence_identity
  COMMAND "${Python3_EXECUTABLE}" -m unittest
    benchmarks.tools.tests.evidence_identity_tests)
set_tests_properties(clipper2next_gate_evidence_identity PROPERTIES
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  LABELS "gate;architecture;performance")

get_target_property(clipper2next_product_sources clipper2next SOURCES)
set(clipper2next_internal_test_sources)
foreach(clipper2next_product_source IN LISTS clipper2next_product_sources)
  if(IS_ABSOLUTE "${clipper2next_product_source}" OR
     clipper2next_product_source MATCHES "^\\$<")
    list(APPEND clipper2next_internal_test_sources
      "${clipper2next_product_source}")
  else()
    list(APPEND clipper2next_internal_test_sources
      "${PROJECT_SOURCE_DIR}/${clipper2next_product_source}")
  endif()
endforeach()
add_library(clipper2next_internal_test_support STATIC
  ${clipper2next_internal_test_sources}
)
target_compile_features(clipper2next_internal_test_support PUBLIC cxx_std_23)
target_link_libraries(clipper2next_internal_test_support
  PUBLIC clipper2next::geotypes
)
target_include_directories(clipper2next_internal_test_support
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_compile_definitions(clipper2next_internal_test_support
  PRIVATE
    CLIPPER2NEXT_BUILDING_LIBRARY
    $<$<BOOL:${CLIPPER2NEXT_ENABLE_CLEANUP_LOCAL_COMPACT}>:CLIPPER2NEXT_USE_CLEANUP_LOCAL_COMPACT>
)
clipper2next_target_warnings(clipper2next_internal_test_support)
clipper2next_target_sanitizers(clipper2next_internal_test_support)

foreach(clipper2next_gtest_target IN ITEMS gtest gtest_main)
  if(TARGET ${clipper2next_gtest_target})
    get_target_property(
      clipper2next_gtest_imported ${clipper2next_gtest_target} IMPORTED)
    if(NOT clipper2next_gtest_imported)
      clipper2next_target_sanitizers(${clipper2next_gtest_target})
    endif()
  endif()
endforeach()

set(clipper2next_api_test_sources
  api/borrowed_offset_api_tests.cpp
  api/borrowed_topology_api_tests.cpp
  api/checked_request_api_tests.cpp
  api/clip_request_api_tests.cpp
  api/demo_model_tests.cpp
  api/error_model_tests.cpp
  api/execution_api_tests.cpp
  api/example_smoke_tests.cpp
  api/geotypes_contract_tests.cpp
  api/memory_api_tests.cpp
  api/offset_request_api_tests.cpp
  api/prepared_clip_request_tests.cpp
  api/public_header_tests.cpp
  api/rectclip_request_api_tests.cpp
  api/value_type_api_tests.cpp
)

set(clipper2next_core_test_sources
  core/canonicalize_tests.cpp
  core/geometry_algorithms_tests.cpp
  core/geometry_predicates_tests.cpp
  core/scalar_helper_tests.cpp
  core/scaling_error_tests.cpp
)

set(clipper2next_clip_test_sources
  clip/closed_clip_fast_path_tests.cpp
  clip/minkowski_tests.cpp
  clip/union_component_grouping_tests.cpp
  clip/union_component_guard_tests.cpp
)

set(clipper2next_clip_engine_test_sources
  clip_engine/engine_active_list_tests.cpp
  clip_engine/engine_ael_builder_tests.cpp
  clip_engine/engine_fill_rule_tests.cpp
  clip_engine/engine_geometry_tests.cpp
  clip_engine/engine_graph_tests.cpp
  clip_engine/engine_horizontal_tests.cpp
  clip_engine/engine_intersection_processor_tests.cpp
  clip_engine/engine_intersections_tests.cpp
  clip_engine/engine_lifecycle_tests.cpp
  clip_engine/engine_output_cleanup_tests.cpp
  clip_engine/engine_output_tests.cpp
  clip_engine/engine_output_topology_tests.cpp
  clip_engine/engine_runtime_views_tests.cpp
  clip_engine/engine_scanbeam_tests.cpp
  clip_engine/engine_scanbeam_schedule_tests.cpp
  clip_engine/engine_scanline_tests.cpp
  clip_engine/engine_topology_tests.cpp
  clip_engine/engine_types_tests.cpp
  clip_engine/engine_winding_tests.cpp
  clip_engine/poly_tree_tests.cpp
)

set(clipper2next_support_test_sources
  support/batch_determinism_tests.cpp
  support/batch_executor_tests.cpp
  support/checked_size_tests.cpp
  support/stable_pool_tests.cpp
  support/topology_store_tests.cpp
)

set(clipper2next_fixture_test_sources
  fuzz/fuzz_smoke_tests.cpp
  golden/golden_fixture_tests.cpp
  property/property_tests.cpp
)

set(clipper2next_offset_test_sources
  offset/offset_builder_tests.cpp
  offset/offset_execution_tests.cpp
  offset/offset_fast_path_tests.cpp
  offset/offset_geometry_tests.cpp
  offset/offset_group_tests.cpp
  offset/offset_join_processor_tests.cpp
  offset/offset_output_tests.cpp
  offset/offset_state_tests.cpp
  oracle/offset_backend_equivalence_tests.cpp
)

set(clipper2next_rectclip_test_sources
  rectclip/rectclip_classifier_tests.cpp
  rectclip/rectclip_executor_tests.cpp
  rectclip/rectclip_graph_tests.cpp
  rectclip/rectclip_intersections_tests.cpp
  rectclip/rectclip_traversal_tests.cpp
)

set(clipper2next_triangulation_test_sources
  triangulation/triangulation_boundary_tests.cpp
  triangulation/triangulation_context_tests.cpp
  triangulation/triangulation_delaunay_tests.cpp
  triangulation/triangulation_edge_graph_tests.cpp
  triangulation/triangulation_executor_tests.cpp
  triangulation/triangulation_graph_tests.cpp
  triangulation/triangulation_intersections_tests.cpp
  triangulation/triangulation_legalizer_tests.cpp
  triangulation/triangulation_path_builder_tests.cpp
  triangulation/triangulation_result_builder_tests.cpp
  triangulation/triangulation_sweep_line_tests.cpp
  triangulation/triangulation_types_tests.cpp
)

set(clipper2next_test_sources
  ${clipper2next_api_test_sources}
  ${clipper2next_core_test_sources}
  ${clipper2next_clip_test_sources}
  ${clipper2next_clip_engine_test_sources}
  ${clipper2next_support_test_sources}
  ${clipper2next_fixture_test_sources}
  ${clipper2next_offset_test_sources}
  ${clipper2next_rectclip_test_sources}
  ${clipper2next_triangulation_test_sources}
)

add_executable(clipper2next_tests
  ${clipper2next_test_sources}
  ../demo/qml/src/demo_model.cpp
)

target_link_libraries(clipper2next_tests
  PRIVATE
    clipper2next_internal_test_support
    ${clipper2next_gtest_libs}
)
target_include_directories(clipper2next_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/demo/qml/src
)
target_compile_definitions(clipper2next_tests PRIVATE
  CLIPPER2NEXT_BUILDING_LIBRARY
)
clipper2next_target_warnings(clipper2next_tests)
clipper2next_target_sanitizers(clipper2next_tests)
clipper2next_target_msvc_external_pgo(clipper2next_tests CORRECTNESS)

gtest_discover_tests(clipper2next_tests)

add_executable(
  clipper2next_offset_cleanup_failure_contract
  api/offset_cleanup_failure_contract.cpp
)
target_link_libraries(
  clipper2next_offset_cleanup_failure_contract
  PRIVATE clipper2next_internal_test_support
)
target_compile_features(
  clipper2next_offset_cleanup_failure_contract PRIVATE cxx_std_23
)
target_compile_definitions(
  clipper2next_offset_cleanup_failure_contract
  PRIVATE CLIPPER2NEXT_BUILDING_LIBRARY
)
clipper2next_target_warnings(clipper2next_offset_cleanup_failure_contract)
clipper2next_target_sanitizers(clipper2next_offset_cleanup_failure_contract)
add_test(
  NAME clipper2next_offset_cleanup_failure_contract
  COMMAND clipper2next_offset_cleanup_failure_contract
)

if(WIN32)
  find_program(clipper2next_export_inspector NAMES dumpbin REQUIRED)
else()
  find_program(clipper2next_export_inspector NAMES nm llvm-nm REQUIRED)
endif()
add_test(NAME clipper2next_export_boundary
  COMMAND ${CMAKE_COMMAND}
    "-DLIBRARY_FILE=$<TARGET_FILE:clipper2next>"
    "-DEXPORT_INSPECTOR=${clipper2next_export_inspector}"
    -P "${CMAKE_CURRENT_LIST_DIR}/verify_dynamic_exports.cmake"
)

set(clipper2next_install_smoke_env)
foreach(clipper2next_install_smoke_env_var IN ITEMS PATH INCLUDE LIB LIBPATH)
  if(DEFINED ENV{${clipper2next_install_smoke_env_var}})
    string(
      REPLACE ";"
      "\\;"
      clipper2next_install_smoke_env_value
      "$ENV{${clipper2next_install_smoke_env_var}}")
    list(
      APPEND
      clipper2next_install_smoke_env
      "${clipper2next_install_smoke_env_var}=${clipper2next_install_smoke_env_value}")
  endif()
endforeach()

set(clipper2next_install_smoke_build_type_arg)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(clipper2next_install_smoke_build_type_arg
    "-DCLIPPER2NEXT_CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

add_test(NAME clipper2next_install_tree_smoke
  COMMAND ${CMAKE_COMMAND}
    -E env ${clipper2next_install_smoke_env}
    ${CMAKE_COMMAND}
    "-DCLIPPER2NEXT_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DCLIPPER2NEXT_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/install_smoke_prefix/$<CONFIG>"
    "-DCLIPPER2NEXT_INSTALL_SMOKE_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/install_smoke_project"
    "-DCLIPPER2NEXT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DCLIPPER2NEXT_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
    "-DCLIPPER2NEXT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCLIPPER2NEXT_INSTALL_SMOKE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/install_smoke_build/$<CONFIG>"
    "-DCLIPPER2NEXT_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
    "-DCLIPPER2NEXT_CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DCLIPPER2NEXT_VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
    "-DCLIPPER2NEXT_VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
    ${clipper2next_install_smoke_build_type_arg}
    -P "${CMAKE_CURRENT_LIST_DIR}/install_smoke_project/run_install_smoke.cmake"
)

if(CLIPPER2NEXT_BUILD_ORACLE)
  set(clipper2next_oracle_test_sources
    oracle/differential_clip_tests.cpp
    oracle/differential_geometry_algorithms_tests.cpp
    oracle/differential_minkowski_tests.cpp
    oracle/differential_offset_tests.cpp
    oracle/differential_rectclip_tests.cpp
    oracle/differential_transform_tests.cpp
    oracle/differential_triangulation_tests.cpp
    oracle/external_geometry_corpus_tests.cpp
    oracle/geometry_corpus_jsonl_tests.cpp
    oracle/legacy_corpus_tests.cpp
    oracle/external_corpus_tests.cpp
    oracle/issue_regression_tests.cpp
    oracle/path_equivalence_tests.cpp
  )

  add_executable(clipper2next_oracle_tests
    ${clipper2next_oracle_test_sources}
  )

  target_link_libraries(clipper2next_oracle_tests
    PRIVATE
      clipper2next::clipper2next
      ${clipper2next_oracle_legacy_target}
      ${clipper2next_gtest_libs}
  )
  target_include_directories(clipper2next_oracle_tests
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(clipper2next_oracle_tests
    PRIVATE
      CLIPPER2NEXT_DEFAULT_GEOMETRY_CORPUS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/oracle/corpus/geometry"
  )
  clipper2next_target_warnings(clipper2next_oracle_tests)
  clipper2next_target_sanitizers(clipper2next_oracle_tests)
  clipper2next_target_msvc_external_pgo(clipper2next_oracle_tests CORRECTNESS)

  gtest_discover_tests(clipper2next_oracle_tests)

endif()

if(CLIPPER2NEXT_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CLIPPER2NEXT_BUILD_FUZZERS requires Clang libFuzzer support")
  endif()

  add_executable(clipper2next_clip_fuzzer
    fuzz/clip_fuzzer.cpp
  )
  target_link_libraries(clipper2next_clip_fuzzer
    PRIVATE
      clipper2next::clipper2next
  )
  target_include_directories(clipper2next_clip_fuzzer
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_options(clipper2next_clip_fuzzer PRIVATE -fsanitize=fuzzer)
  target_link_options(clipper2next_clip_fuzzer PRIVATE -fsanitize=fuzzer)
  clipper2next_target_warnings(clipper2next_clip_fuzzer)
  clipper2next_target_sanitizers(clipper2next_clip_fuzzer)
endif()
