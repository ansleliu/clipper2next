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
if(CLIPPER2NEXT_BUILD_ORACLE)
  clipper2next_add_python_gate(vendored_geometry_profiles
    tools/release/prepare_correction_corpus.py
    --check-selection "${corpus_selection_path}" --selection-sha256 "${corpus_selection_sha256}")
else()
  clipper2next_add_python_gate(vendored_geometry_profiles
    tools/release/prepare_correction_corpus.py
    --source "${PROJECT_SOURCE_DIR}/tests/oracle/corpus/geometry"
    --output-dir "${clipper2next_gate_output_dir}/vendored-corpus")
endif()

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
function(clipper2next_add_internal_test_support target)
  add_library(${target} STATIC ${clipper2next_internal_test_sources})
  target_compile_features(${target} PUBLIC cxx_std_23)
  target_link_libraries(${target} PUBLIC clipper2next::geotypes)
  target_include_directories(${target}
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(${target} PRIVATE
    CLIPPER2NEXT_BUILDING_LIBRARY
    $<$<BOOL:${CLIPPER2NEXT_ENABLE_CLEANUP_LOCAL_COMPACT}>:CLIPPER2NEXT_USE_CLEANUP_LOCAL_COMPACT>)
  clipper2next_target_warnings(${target})
  clipper2next_target_sanitizers(${target})
endfunction()

clipper2next_add_internal_test_support(clipper2next_internal_test_support)

add_executable(clipper2next_triangulation_phase_probe triangulation/triangulation_phase_probe.cpp)
target_link_libraries(clipper2next_triangulation_phase_probe PRIVATE clipper2next_internal_test_support)
target_include_directories(clipper2next_triangulation_phase_probe PRIVATE
  ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(clipper2next_triangulation_phase_probe PRIVATE CLIPPER2NEXT_BUILDING_LIBRARY)
clipper2next_target_warnings(clipper2next_triangulation_phase_probe)
clipper2next_target_sanitizers(clipper2next_triangulation_phase_probe)

foreach(contract IN ITEMS status graph)
  if(contract STREQUAL "status")
    set(partition_source triangulation/triangulation_partition_status_contract.cpp)
  else()
    set(partition_source triangulation/triangulation_partition_contract.cpp)
  endif()
  set(partition_target clipper2next_triangulation_partition_${contract}_contract)
  add_executable(${partition_target} ${partition_source})
  target_link_libraries(${partition_target} PRIVATE clipper2next_internal_test_support)
  target_include_directories(${partition_target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(${partition_target} PRIVATE CLIPPER2NEXT_BUILDING_LIBRARY)
  clipper2next_target_warnings(${partition_target})
  clipper2next_target_sanitizers(${partition_target})
  add_test(NAME ${partition_target} COMMAND ${partition_target})
endforeach()

add_executable(clipper2next_triangulation_coordinate_identity_contract
  triangulation/triangulation_coordinate_identity_contract.cpp)
target_link_libraries(clipper2next_triangulation_coordinate_identity_contract PRIVATE
  clipper2next_internal_test_support)
target_include_directories(clipper2next_triangulation_coordinate_identity_contract PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(clipper2next_triangulation_coordinate_identity_contract PRIVATE
  CLIPPER2NEXT_BUILDING_LIBRARY)
clipper2next_target_warnings(clipper2next_triangulation_coordinate_identity_contract)
clipper2next_target_sanitizers(clipper2next_triangulation_coordinate_identity_contract)
add_test(NAME clipper2next_triangulation_coordinate_identity_contract
  COMMAND clipper2next_triangulation_coordinate_identity_contract)

add_executable(clipper2next_exact_contract_probe triangulation/exact_contract_probe.cpp)
target_link_libraries(clipper2next_exact_contract_probe PRIVATE clipper2next_internal_test_support)
target_include_directories(clipper2next_exact_contract_probe PRIVATE
  ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(clipper2next_exact_contract_probe PRIVATE CLIPPER2NEXT_BUILDING_LIBRARY)
clipper2next_target_warnings(clipper2next_exact_contract_probe)
clipper2next_target_sanitizers(clipper2next_exact_contract_probe)
add_test(NAME clipper2next_exact_predicate_oracle
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/triangulation/exact_predicate_oracle.py"
    --probe "$<TARGET_FILE:clipper2next_exact_contract_probe>")


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
  api/geotypes_validation_tests.cpp
  api/path_set_builder_contract_tests.cpp
  api/memory_api_tests.cpp
  api/offset_request_api_tests.cpp
  api/offset_error_contract_tests.cpp
  api/prepared_clip_request_tests.cpp
  api/public_header_tests.cpp
  api/rectclip_request_api_tests.cpp
  api/value_type_api_tests.cpp
)

set(clipper2next_core_test_sources
  core/canonicalize_tests.cpp
  core/checked_coordinate_tests.cpp
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
  clip_engine/engine_buffer_tests.cpp
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
  offset/offset_fast_path_tests.cpp
  offset/offset_geometry_tests.cpp
  offset/offset_group_tests.cpp
  offset/offset_join_processor_tests.cpp
  offset/offset_output_tests.cpp
  offset/offset_state_tests.cpp
  oracle/offset_facade_integration_tests.cpp
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
  triangulation/triangulation_catalog_tests.cpp
  triangulation/triangulation_segment_query_tests.cpp
  triangulation/triangulation_context_tests.cpp
  triangulation/triangulation_mesh_tests.cpp
  triangulation/triangulation_simple_tests.cpp
  triangulation/triangulation_delaunay_tests.cpp
  triangulation/triangulation_edge_graph_tests.cpp
  triangulation/triangulation_executor_tests.cpp
  triangulation/triangulation_intersections_tests.cpp
  triangulation/triangulation_legalizer_tests.cpp
  triangulation/triangulation_sweep_front_tests.cpp
  triangulation/triangulation_sweep_diagonal_tests.cpp
  triangulation/triangulation_boundary_sector_tests.cpp
  triangulation/triangulation_source_segment_tests.cpp
  triangulation/exact_distance_query_tests.cpp
  triangulation/triangulation_predicate_domain_tests.cpp
  triangulation/oriented_in_circle_tests.cpp
  triangulation/triangulation_path_builder_tests.cpp
  triangulation/triangulation_result_builder_tests.cpp
  triangulation/triangulation_sweep_line_tests.cpp
  triangulation/triangulation_active_endpoint_tests.cpp
  triangulation/triangulation_incident_storage_tests.cpp
  triangulation/triangulation_complete_query_tests.cpp
  triangulation/triangulation_graph_orientation_tests.cpp
  triangulation/triangulation_catalog_pair_tests.cpp
  triangulation/triangulation_convex_initialization_tests.cpp
  triangulation/triangulation_monotone_projection_tests.cpp
  triangulation/triangulation_monotone_workspace_tests.cpp
  triangulation/triangulation_preparation_contract_tests.cpp
  triangulation/triangulation_source_contact_facts_tests.cpp
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

# This TU compiles the production owning implementation to observe its existing
# private TLS state. Its definitions satisfy the support archive's owning symbols.
add_executable(clipper2next_owning_clip_empty_phase_contract
  api/owning_clip_empty_phase_contract.cpp)
target_link_libraries(clipper2next_owning_clip_empty_phase_contract PRIVATE
  clipper2next_internal_test_support ${clipper2next_gtest_libs})
target_include_directories(clipper2next_owning_clip_empty_phase_contract PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(clipper2next_owning_clip_empty_phase_contract PRIVATE
  CLIPPER2NEXT_BUILDING_LIBRARY)
clipper2next_target_warnings(clipper2next_owning_clip_empty_phase_contract)
clipper2next_target_sanitizers(clipper2next_owning_clip_empty_phase_contract)
gtest_discover_tests(clipper2next_owning_clip_empty_phase_contract)

add_executable(clipper2next_triangulation_failure_contract api/triangulation_failure_contract.cpp)
add_executable(clipper2next_triangulation_partition_failure_contract
  api/triangulation_partition_failure_contract.cpp)
foreach(triangulation_failure_target IN ITEMS
    clipper2next_triangulation_failure_contract
    clipper2next_triangulation_partition_failure_contract)
  target_link_libraries(${triangulation_failure_target} PRIVATE clipper2next_internal_test_support)
  target_include_directories(${triangulation_failure_target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(${triangulation_failure_target} PRIVATE CLIPPER2NEXT_BUILDING_LIBRARY)
  clipper2next_target_warnings(${triangulation_failure_target})
  clipper2next_target_sanitizers(${triangulation_failure_target})
  add_test(NAME ${triangulation_failure_target} COMMAND ${triangulation_failure_target})
endforeach()

add_executable(
  clipper2next_offset_cleanup_failure_contract
  api/offset_cleanup_failure_contract.cpp
  support/allocation_failure_probe.cpp
)
add_executable(clipper2next_prepared_failure_contract
  api/prepared_failure_contract.cpp
  support/allocation_failure_probe.cpp)
add_executable(clipper2next_checked_failure_contract
  api/checked_failure_contract.cpp
  support/engine_execution_override.cpp
  support/allocation_failure_probe.cpp)
target_include_directories(clipper2next_checked_failure_contract PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_executable(clipper2next_direct_materialization_contract
  api/direct_materialization_contract.cpp support/allocation_failure_probe.cpp)
target_include_directories(clipper2next_direct_materialization_contract PRIVATE ${PROJECT_SOURCE_DIR}/src)
set(clipper2next_oom_support clipper2next_internal_test_support)
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  # MSVC's checked-iterator proxy allocation occurs inside noexcept STL
  # constructors. Global new failure there terminates before library catches
  # can execute. Only these fault-injection probes use regular STL bookkeeping;
  # the product DLL and all other Debug tests keep their default iterator ABI.
  clipper2next_add_internal_test_support(clipper2next_oom_test_support)
  target_compile_definitions(clipper2next_oom_test_support
    PUBLIC _ITERATOR_DEBUG_LEVEL=0)
  set(clipper2next_oom_support clipper2next_oom_test_support)
endif()
foreach(clipper2next_oom_probe IN ITEMS
    clipper2next_offset_cleanup_failure_contract clipper2next_prepared_failure_contract
    clipper2next_checked_failure_contract clipper2next_direct_materialization_contract)
  target_link_libraries(${clipper2next_oom_probe} PRIVATE ${clipper2next_oom_support})
  target_compile_features(${clipper2next_oom_probe} PRIVATE cxx_std_23)
  target_compile_definitions(${clipper2next_oom_probe} PRIVATE CLIPPER2NEXT_BUILDING_LIBRARY)
  target_include_directories(${clipper2next_oom_probe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  clipper2next_target_warnings(${clipper2next_oom_probe})
  # This contract intentionally replaces the complete global new/delete family
  # with malloc/free-backed fault injection. GCC 14 diagnoses that deliberate
  # replacement as a mismatched allocation pair after inlining standard-library
  # callers; keep the exception local to this test target.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(
      ${clipper2next_oom_probe}
      PRIVATE -Wno-mismatched-new-delete
    )
  endif()
  clipper2next_target_sanitizers(${clipper2next_oom_probe})
  add_test(NAME ${clipper2next_oom_probe} COMMAND ${clipper2next_oom_probe})
endforeach()

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
    oracle/offset_contract.cpp
    oracle/triangulation_contract.cpp
    ${PROJECT_SOURCE_DIR}/packaging/test_package/correction_contract.cpp
  )

  target_link_libraries(clipper2next_oracle_tests
    PRIVATE
      clipper2next::clipper2next
      ${clipper2next_oracle_legacy_target}
      ${clipper2next_gtest_libs}
      ${CMAKE_DL_LIBS}
  )
  target_include_directories(clipper2next_oracle_tests
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(clipper2next_oracle_tests
    PRIVATE
      CLIPPER2NEXT_DEFAULT_GEOMETRY_CORPUS_ROOT="${CLIPPER2NEXT_SELECTED_CORPUS_ROOT}"
      CLIPPER2NEXT_OFFSET_CONTRACT_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/offset/offset_contract_cases.jsonl"
  )
  clipper2next_target_warnings(clipper2next_oracle_tests)
  clipper2next_target_sanitizers(clipper2next_oracle_tests)
  clipper2next_target_msvc_external_pgo(clipper2next_oracle_tests CORRECTNESS)

  gtest_discover_tests(clipper2next_oracle_tests)
  foreach(producer IN ITEMS differential corpus)
    if(producer STREQUAL "differential")
      set(offset_contract_filter "Clipper2NextDifferentialOffsetTests.AbsoluteContractsRecordBothActualEngines")
    else()
      set(offset_contract_filter "ExternalGeometryCorpus.OffsetVerificationCorpusExecutesAgainstLegacy")
    endif()
    set(offset_evidence_dir "${CMAKE_CURRENT_BINARY_DIR}/offset-contract-${producer}")
    add_test(NAME clipper2next_offset_contract_oracle_${producer}
      COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/release/run_offset_contract_preflight.py"
        --producer "$<TARGET_FILE:clipper2next_oracle_tests>"
        "--producer-argument=--gtest_filter=${offset_contract_filter}"
        "--producer-argument=--gtest_output=xml:${offset_evidence_dir}/producer.xml"
        --output-dir "${offset_evidence_dir}")
  endforeach()
  set(tri_evidence_dir "${CMAKE_CURRENT_BINARY_DIR}/triangulation-contract")
  add_test(NAME clipper2next_triangulation_contract_producer
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/release/run_triangulation_contract_preflight.py"
      --producer "$<TARGET_FILE:clipper2next_oracle_tests>"
      "--producer-argument=--gtest_filter=Clipper2NextDifferentialTriangulationTests.ActualBoundaryContractsRecordBothEngines"
      --expected-count 124 --capture-only --output-dir "${tri_evidence_dir}")
  set_tests_properties(clipper2next_triangulation_contract_producer PROPERTIES
    FIXTURES_SETUP triangulation_actual_records)
  add_test(NAME clipper2next_triangulation_contract_oracle
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/triangulation/triangulation_contract_oracle.py"
      --records "${tri_evidence_dir}/actual-records.log" --expected-count 124
      --result "${tri_evidence_dir}/triangulation-contract-result.json")
  set_tests_properties(clipper2next_triangulation_contract_oracle PROPERTIES
    FIXTURES_REQUIRED triangulation_actual_records DEPENDS clipper2next_triangulation_contract_producer)
  add_test(NAME clipper2next_triangulation_contract_oracle_corpus
    COMMAND "${Python3_EXECUTABLE}" "${CLIPPER2NEXT_TRIANGULATION_PREFLIGHT}"
      --producer "$<TARGET_FILE:clipper2next_oracle_tests>"
      "--producer-argument=--gtest_filter=ExternalGeometryCorpus.TriangulationVerificationCorpusExecutesAgainstLegacy"
      --expected-count 240 ${CLIPPER2NEXT_CORPUS_PREFLIGHT_ARGS} --output-dir "${tri_evidence_dir}-corpus")
  add_test(NAME clipper2next_triangulation_contract_rejections
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/triangulation/triangulation_contract_oracle_tests.py")
  add_test(NAME clipper2next_offset_contract_source_bindings
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/tests/offset_contract_source_tests.py")
  add_test(NAME clipper2next_offset_contract_record_rejections
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/offset/offset_contract_oracle_tests.py")


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

# Cleanup is tested with the ordinary Debug iterator ABI and persistent rejection.
add_executable(clipper2next_cache_release_no_allocation_contract
  api/cache_release_no_allocation_contract.cpp support/allocation_failure_probe.cpp)
target_link_libraries(clipper2next_cache_release_no_allocation_contract PRIVATE clipper2next_internal_test_support)
target_include_directories(clipper2next_cache_release_no_allocation_contract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(clipper2next_cache_release_no_allocation_contract PRIVATE CLIPPER2NEXT_BUILDING_LIBRARY CLIPPER2NEXT_NO_ALLOCATION_CONTRACT)
clipper2next_target_warnings(clipper2next_cache_release_no_allocation_contract)
clipper2next_target_sanitizers(clipper2next_cache_release_no_allocation_contract)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(clipper2next_cache_release_no_allocation_contract PRIVATE -Wno-mismatched-new-delete)
endif()
add_test(NAME clipper2next_cache_release_no_allocation_contract COMMAND clipper2next_cache_release_no_allocation_contract)
