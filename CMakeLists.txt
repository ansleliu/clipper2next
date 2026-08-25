cmake_minimum_required(VERSION 3.24)

project(clipper2next VERSION 4.0.0 LANGUAGES CXX)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
  message(FATAL_ERROR
    "clipper2next requires the Ninja generator on every supported platform; "
    "actual generator: ${CMAKE_GENERATOR}")
endif()

# Ninja is deliberately the only generator, so every runtime artifact has one
# deterministic location. On Windows this keeps the DLL beside all test,
# benchmark, demo, and tool executables without stale post-build copies.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/Clipper2NextWarnings.cmake)

option(CLIPPER2NEXT_TESTS "Build clipper2next tests" ON)
option(CLIPPER2NEXT_BENCHMARKS "Build clipper2next benchmarks" ON)
option(CLIPPER2NEXT_DEMOS "Build clipper2next demo applications" OFF)
option(CLIPPER2NEXT_QML_DEMO "Build the Qt Quick clipper2next demo" ${CLIPPER2NEXT_DEMOS})
option(CLIPPER2NEXT_FETCH_DEPS "Fetch missing third-party dependencies" OFF)
option(CLIPPER2NEXT_ENABLE_CLEANUP_LOCAL_COMPACT
  "Enable experimental output cleanup local compaction helper" OFF)
option(CLIPPER2NEXT_WARNINGS_AS_ERRORS
  "Treat clipper2next warnings as errors" OFF)
option(CLIPPER2NEXT_ENABLE_ASAN
  "Enable AddressSanitizer instrumentation for clipper2next targets" OFF)
option(CLIPPER2NEXT_ENABLE_UBSAN
  "Enable UndefinedBehaviorSanitizer instrumentation for clipper2next targets" OFF)
option(CLIPPER2NEXT_ENABLE_TSAN
  "Enable ThreadSanitizer instrumentation for clipper2next targets" OFF)
option(CLIPPER2NEXT_BUILD_ORACLE
  "Build optional Clipper2Lib oracle tests and benchmarks" OFF)
option(CLIPPER2NEXT_BUILD_FUZZERS
  "Build optional Clang libFuzzer targets for clipper2next" OFF)
set(CLIPPER2NEXT_CXX_STANDARD 23 CACHE STRING "C++ standard required by clipper2next")
set(CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE "OFF" CACHE STRING
  "Target-scoped MSVC external benchmark PGO mode: OFF, INSTRUMENT, or OPTIMIZE")
set_property(CACHE CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE PROPERTY STRINGS
  OFF INSTRUMENT OPTIMIZE)
set(CLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE "" CACHE FILEPATH
  "Explicit PGD shared by the external benchmark and optimized correctness targets")

set(clipper2next_msvc_external_pgo_modes OFF INSTRUMENT OPTIMIZE)
if(NOT CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE IN_LIST clipper2next_msvc_external_pgo_modes)
  message(FATAL_ERROR
    "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE must be OFF, INSTRUMENT, or OPTIMIZE")
endif()
if(NOT CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE STREQUAL "OFF")
  if(NOT MSVC)
    message(FATAL_ERROR "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE requires MSVC")
  endif()
  if(CLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE STREQUAL "")
    message(FATAL_ERROR
      "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE is required when MSVC PGO is enabled")
  endif()
endif()

function(clipper2next_target_msvc_external_pgo target_name target_role)
  if(CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE STREQUAL "OFF")
    return()
  endif()
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "cannot apply MSVC PGO to missing target ${target_name}")
  endif()
  if(NOT target_role MATCHES "^(LIBRARY|BENCHMARK|CORRECTNESS)$")
    message(FATAL_ERROR "invalid MSVC PGO role ${target_role} for ${target_name}")
  endif()

  target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:/GL>)
  if(CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE STREQUAL "INSTRUMENT" AND
     target_role STREQUAL "BENCHMARK")
    target_compile_definitions(${target_name} PRIVATE
      $<$<CONFIG:Release>:CLIPPER2NEXT_MSVC_PGO_INSTRUMENTED=1>)
    target_link_libraries(${target_name} PRIVATE pgobootrun)
  endif()
  if(target_role STREQUAL "LIBRARY")
    return()
  endif()
  if(CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE STREQUAL "INSTRUMENT" AND
     NOT target_role STREQUAL "BENCHMARK")
    return()
  endif()

  set(clipper2next_pgd_option "/PGD:${CLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE}")
  if(CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE STREQUAL "INSTRUMENT")
    target_link_options(${target_name} PRIVATE
      $<$<CONFIG:Release>:/LTCG:PGINSTRUMENT>
      $<$<CONFIG:Release>:/GENPROFILE>
      $<$<CONFIG:Release>:${clipper2next_pgd_option}>)
  else()
    target_link_options(${target_name} PRIVATE
      $<$<CONFIG:Release>:/LTCG:PGOPTIMIZE>
      $<$<CONFIG:Release>:/USEPROFILE>
      $<$<CONFIG:Release>:${clipper2next_pgd_option}>)
  endif()
endfunction()

if(NOT CLIPPER2NEXT_CXX_STANDARD STREQUAL "23")
  message(FATAL_ERROR "clipper2next requires CLIPPER2NEXT_CXX_STANDARD=23")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
  [=[
#include <expected>
#ifndef __cpp_lib_expected
#error "std::expected feature-test macro is unavailable"
#endif
#if __cpp_lib_expected < 202202L
#error "std::expected implementation is too old"
#endif
int main() {
  std::expected<int, int> result{1};
  return result.value();
}
]=]
  CLIPPER2NEXT_HAS_STD_EXPECTED)
if(NOT CLIPPER2NEXT_HAS_STD_EXPECTED)
  message(FATAL_ERROR
    "clipper2next requires C++23 std::expected; select a standard library with full support")
endif()

if(CLIPPER2NEXT_BUILD_ORACLE)
  find_package(Clipper2 CONFIG REQUIRED)
  if(NOT TARGET Clipper2::Clipper2)
    message(FATAL_ERROR "Clipper2 package did not export Clipper2::Clipper2")
  endif()
  set(clipper2next_oracle_legacy_target Clipper2::Clipper2)

endif()

add_library(clipper2next_geotypes INTERFACE)
add_library(clipper2next::geotypes ALIAS clipper2next_geotypes)
set_target_properties(clipper2next_geotypes PROPERTIES EXPORT_NAME geotypes)
target_compile_features(clipper2next_geotypes INTERFACE cxx_std_23)
target_include_directories(clipper2next_geotypes
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_sources(clipper2next_geotypes
  INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/clipper2next/geotypes/geotypes.hpp
      include/clipper2next/geotypes/coordinate.hpp
      include/clipper2next/geotypes/coordinate_arithmetic.hpp
      include/clipper2next/geotypes/coordinate_cast.hpp
      include/clipper2next/geotypes/coordinate_midpoint.hpp
      include/clipper2next/geotypes/path.hpp
      include/clipper2next/geotypes/point.hpp
      include/clipper2next/geotypes/rect.hpp
      include/clipper2next/geotypes/topology.hpp
)

add_library(clipper2next SHARED)
add_library(clipper2next::clipper2next ALIAS clipper2next)

target_compile_features(clipper2next PUBLIC cxx_std_23)
target_link_libraries(clipper2next PUBLIC clipper2next::geotypes)

target_sources(clipper2next
  PRIVATE
    src/api/release_thread_caches.cpp
    src/api/engine_resource_plan.cpp
    src/api/request_execution_helpers.cpp
    src/api/requests_topology.cpp
    src/api/requests_topology_execute.cpp
    src/api/requests_topology_input.cpp
    src/api/requests_topology_output.cpp
    src/api/requests_topology_state.cpp
    src/api/request_metadata.cpp
    src/api/requests.cpp
    src/api/requests_offset.cpp
    src/api/requests_offset_owning.cpp
    src/api/requests_rectclip.cpp
    src/batch/batch.cpp
    src/batch/batch_checked.cpp
    src/batch/batch_clip_executor.cpp
    src/clip/boolean_union_service.cpp
    src/clip/clip_execution_strategy.cpp
    src/clip/clip_tree_fast_path.cpp
    src/clip/clip_union_component_guards.cpp
    src/clip/closed_clip_contained_intersection.cpp
    src/clip/closed_clip_fast_path.cpp
    src/clip/closed_clip_rectangle_fast_path.cpp
    src/clip/closed_clip_rectangle_operations.cpp
    src/clip/closed_clip_rectangle_xor_fast_path.cpp
    src/clip/engine/engine_ael_builder.cpp
    src/clip/engine/engine_closed_intersection_processor.cpp
    src/clip/engine/engine_graph.cpp
    src/clip/engine/engine_horizontal.cpp
    src/clip/engine/engine_horizontal_join_builder.cpp
    src/clip/engine/engine_horizontal_join_processor.cpp
    src/clip/engine/engine_horizontal_scanbeam.cpp
    src/clip/engine/engine_horizontal_segments.cpp
    src/clip/engine/engine_horizontal_span.cpp
    src/clip/engine/engine_intersection_processor.cpp
    src/clip/engine/engine_intersections.cpp
    src/clip/engine/engine_intersections_contiguous.cpp
    src/clip/engine/engine_lifecycle.cpp
    src/clip/engine/engine_lifecycle_paths.cpp
    src/clip/engine/engine_open_intersection_processor.cpp
    src/clip/engine/engine_output.cpp
    src/clip/engine/engine_output_cleanup.cpp
    src/clip/engine/engine_output_cleanup_bounds.cpp
    src/clip/engine/engine_output_cleanup_compact.cpp
    src/clip/engine/engine_output_cleanup_intersections.cpp
    src/clip/engine/engine_output_cleanup_owners.cpp
    src/clip/engine/engine_output_cleanup_split.cpp
    src/clip/engine/engine_output_join_paths.cpp
    src/clip/engine/engine_output_owner.cpp
    src/clip/engine/engine_output_topology.cpp
    src/clip/engine/engine_path_builder.cpp
    src/clip/engine/engine_scanbeam_orchestrator.cpp
    src/clip/engine/engine_scanbeam_orchestrator_helpers.cpp
    src/clip/engine/engine_scanbeam_processor.cpp
    src/clip/engine/engine_scanbeam_schedule.cpp
    src/clip/engine/engine_scanbeam_services.cpp
    src/clip/engine/engine_scanline.cpp
    src/clip/engine/engine_topology.cpp
    src/clip/engine/engine_topology_clean_path.cpp
    src/clip/engine/engine_topology_point_query.cpp
    src/clip/engine/engine_winding.cpp
    src/geometry/geometry_algorithms.cpp
    src/geometry/geometry_polygon_predicates.cpp
    src/geometry/geometry_predicates.cpp
    src/geometry/geometry_predicate_api.cpp
    src/geometry/geometry_shape_algorithms.cpp
    src/geometry/translate.cpp
    src/minkowski/minkowski.cpp
    src/minkowski/minkowski_kernel.cpp
    src/offset/offset_algorithm.cpp
    src/offset/offset_algorithm_flat.cpp
    src/offset/offset_builder.cpp
    src/offset/offset_direct_guards.cpp
    src/offset/offset_execution_context.cpp
    src/offset/offset_executor.cpp
    src/offset/offset_fast_path.cpp
    src/offset/offset_group_parallel.cpp
    src/offset/offset_group_processor.cpp
    src/offset/offset_group_state.cpp
    src/offset/offset_join_processor.cpp
    src/offset/offset_open_path_processor.cpp
    src/offset/offset_path_processor.cpp
    src/offset/offset_thread_state.cpp
    src/polygon/polygon_tree.cpp
    src/rectclip/rectclip_thread_state.cpp
    src/rectclip/rectclip_classifier.cpp
    src/rectclip/rectclip_edge_check.cpp
    src/rectclip/rectclip_edge_tidy.cpp
    src/rectclip/rectclip_edges.cpp
    src/rectclip/rectclip_execution_context.cpp
    src/rectclip/rectclip_facade_runner.cpp
    src/rectclip/rectclip_intersections.cpp
    src/rectclip/rectclip_line_executor.cpp
    src/rectclip/rectclip_path_builder.cpp
    src/rectclip/rectclip_path_bounds.cpp
    src/rectclip/rectclip_prepared_bounds.cpp
    src/rectclip/rectclip_polygon_executor.cpp
    src/rectclip/rectclip_traversal.cpp
    src/rectclip/rectclip_unprepared_runner.cpp
    src/triangulation/triangulation.cpp
    src/triangulation/triangulation_cache.cpp
    src/triangulation/triangulation_boundary.cpp
    src/triangulation/triangulation_context.cpp
    src/triangulation/triangulation_delaunay.cpp
    src/triangulation/triangulation_edge_graph.cpp
    src/triangulation/triangulation_executor.cpp
    src/triangulation/triangulation_graph.cpp
    src/triangulation/triangulation_graph_repair.cpp
    src/triangulation/triangulation_intersections.cpp
    src/triangulation/triangulation_legalizer.cpp
    src/triangulation/triangulation_path_builder.cpp
    src/triangulation/triangulation_result_builder.cpp
    src/triangulation/triangulation_sweep.cpp
    src/triangulation/triangulation_sweep_line.cpp
    src/triangulation/triangulation_sweep_triangulators.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/clipper2next/api/error.h
      include/clipper2next/api/execution.h
      include/clipper2next/api/export.h
      include/clipper2next/api/memory.h
      include/clipper2next/api/options.h
      include/clipper2next/api/result.h
      include/clipper2next/batch.h
      include/clipper2next/clip/request.h
      include/clipper2next/clip/borrowed_paths.h
      include/clipper2next/clip/path_source.h
      include/clipper2next/clip/topology.h
      include/clipper2next/clip/topology_writer.h
      include/clipper2next/clip/types.h
      include/clipper2next/clip.h
      include/clipper2next/clipper.h
      include/clipper2next/core/fill_rule.h
      include/clipper2next/core/path.h
      include/clipper2next/core/path_set.h
      include/clipper2next/core/path_set_builder.h
      include/clipper2next/core/point.h
      include/clipper2next/core/rect.h
      include/clipper2next/core.h
      include/clipper2next/geometry/algorithms.h
      include/clipper2next/geometry/core.h
      include/clipper2next/geometry/line_intersections.h
      include/clipper2next/geometry/math.h
      include/clipper2next/geometry/path_transforms.h
      include/clipper2next/geometry/predicates.h
      include/clipper2next/geometry/scale.h
      include/clipper2next/geometry/scaling.h
      include/clipper2next/geometry/scaling_policy.h
      include/clipper2next/geometry/translate.h
      include/clipper2next/geometry.h
      include/clipper2next/minkowski/request.h
      include/clipper2next/minkowski.h
      include/clipper2next/offset/builder.h
      include/clipper2next/offset/borrowed.h
      include/clipper2next/offset/request.h
      include/clipper2next/offset/operations.h
      include/clipper2next/offset/types.h
      include/clipper2next/offset.h
      include/clipper2next/polygon/poly_tree.h
      include/clipper2next/polygon/tree.h
      include/clipper2next/rectclip/request.h
      include/clipper2next/rectclip.h
      include/clipper2next/triangulation/request.h
      include/clipper2next/triangulation.h
      include/clipper2next/version.h
  PRIVATE
    FILE_SET private_headers
    TYPE HEADERS
    BASE_DIRS src
    FILES
      src/api/private/borrowed_topology_pipeline.h
      src/api/private/engine_resource_plan.h
      src/batch/private/batch_clip_executor.h
      src/clip/engine/private/engine_active_list.h
      src/clip/engine/private/engine_ael_builder.h
      src/clip/engine/private/engine_execution_context.h
      src/clip/engine/private/engine_fill_rule.h
      src/clip/engine/private/engine_geometry.h
      src/clip/engine/private/engine_graph.h
      src/clip/engine/private/engine_handles.h
      src/clip/engine/private/engine_horizontal.h
      src/clip/engine/private/engine_intersection_cases.h
      src/clip/engine/private/engine_intersection_processor.h
      src/clip/engine/private/engine_intersections.h
      src/clip/engine/private/engine_lifecycle.h
      src/clip/engine/private/engine_output.h
      src/clip/engine/private/engine_output_cleanup.h
      src/clip/engine/private/engine_output_owner.h
      src/clip/engine/private/engine_output_topology.h
      src/clip/engine/private/engine_path_builder.h
      src/clip/engine/private/engine_runtime_views.h
      src/clip/engine/private/engine_scanbeam_orchestrator.h
      src/clip/engine/private/engine_scanbeam_orchestrator_helpers.h
      src/clip/engine/private/engine_scanbeam_processor.h
      src/clip/engine/private/engine_scanbeam_schedule.h
      src/clip/engine/private/engine_scanbeam_services.h
      src/clip/engine/private/engine_scanline.h
      src/clip/engine/private/engine_state.h
      src/clip/engine/private/engine_topology.h
      src/clip/engine/private/engine_types.h
      src/clip/engine/private/engine_winding.h
      src/clip/private/boolean_union_service.h
      src/clip/private/borrowed_topology_access.h
      src/clip/private/closed_clip_fast_path.h
      src/clip/private/closed_clip_rectangle_operations.h
      src/clip/private/clip_execution_strategy.h
      src/clip/private/clip_request_metadata.h
      src/clip/private/clip_request_validation.h
      src/clip/private/prepared_clip_runtime_data.h
      src/geometry/private/geometry_exact.h
      src/geometry/private/geometry_predicates.h
      src/geometry/private/numeric_policy.h
      src/geometry/private/path_simplicity.h
      src/geometry/private/scalar_helpers.h
      src/minkowski/private/minkowski.h
      src/offset/private/offset_algorithm.h
      src/offset/private/offset_algorithm_support.h
      src/offset/private/offset_execution_context.h
      src/offset/private/offset_executor.h
      src/offset/private/offset_fast_path.h
      src/offset/private/offset_geometry.h
      src/offset/private/offset_group.h
      src/offset/private/offset_group_processor.h
      src/offset/private/offset_join_processor.h
      src/offset/private/offset_open_path_processor.h
      src/offset/private/offset_output.h
      src/offset/private/offset_path_processor.h
      src/offset/private/offset_state.h
      src/offset/private/offset_thread_state.h
      src/rectclip/private/rectclip_path_bounds.h
      src/rectclip/private/rectclip_thread_state.h
      src/rectclip/private/rectclip_classifier.h
      src/rectclip/private/rectclip_context.h
      src/rectclip/private/rectclip_edges.h
      src/rectclip/private/rectclip_execution_context.h
      src/rectclip/private/rectclip_facade_runner.h
      src/rectclip/private/rectclip_graph.h
      src/rectclip/private/rectclip_intersections.h
      src/rectclip/private/rectclip_line_executor.h
      src/rectclip/private/rectclip_path_builder.h
      src/rectclip/private/rectclip_polygon_executor.h
      src/rectclip/private/rectclip_traversal.h
      src/rectclip/private/rectclip_unprepared_runner.h
      src/rectclip/private/rectclip_unprepared_bounds.h
      src/support/private/checked_size.h
      src/support/private/bulk_execution.h
      src/support/private/scoped_nearest_rounding.h
      src/support/private/union_component_grouping.h
      src/support/private/storage/stable_pool.h
      src/support/private/storage/topology_store.h
      src/triangulation/private/triangulation_boundary.h
      src/triangulation/private/triangulation_cache.h
      src/triangulation/private/triangulation_context.h
      src/triangulation/private/triangulation_delaunay.h
      src/triangulation/private/triangulation_edge_graph.h
      src/triangulation/private/triangulation_executor.h
      src/triangulation/private/triangulation_graph.h
      src/triangulation/private/triangulation_graph_repair.h
      src/triangulation/private/triangulation_intersections.h
      src/triangulation/private/triangulation_legalizer.h
      src/triangulation/private/triangulation_path_builder.h
      src/triangulation/private/triangulation_result_builder.h
      src/triangulation/private/triangulation_sweep.h
      src/triangulation/private/triangulation_sweep_line.h
      src/triangulation/private/triangulation_sweep_triangulators.h
      src/triangulation/private/triangulation_topology.h
      src/triangulation/private/triangulation_types.h
)

target_include_directories(clipper2next
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_definitions(clipper2next
  PRIVATE
    CLIPPER2NEXT_BUILDING_LIBRARY
    $<$<BOOL:${CLIPPER2NEXT_ENABLE_CLEANUP_LOCAL_COMPACT}>:CLIPPER2NEXT_USE_CLEANUP_LOCAL_COMPACT>
)

set_target_properties(clipper2next PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(UNIX AND NOT APPLE)
  set(clipper2next_elf_export_map
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clipper2next.exports.map")
  target_link_options(clipper2next PRIVATE
    "LINKER:--version-script=${clipper2next_elf_export_map}")
  set_property(TARGET clipper2next APPEND PROPERTY
    LINK_DEPENDS "${clipper2next_elf_export_map}")
endif()

clipper2next_target_warnings(clipper2next)
clipper2next_target_sanitizers(clipper2next)
clipper2next_target_msvc_external_pgo(clipper2next LIBRARY)

if(CLIPPER2NEXT_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CLIPPER2NEXT_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(CLIPPER2NEXT_DEMOS OR CLIPPER2NEXT_QML_DEMO)
  add_subdirectory(demo)
endif()

install(TARGETS clipper2next_geotypes clipper2next
  EXPORT clipper2next-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
  "${CMAKE_CURRENT_LIST_DIR}/LICENSE_1_0.txt"
  "${CMAKE_CURRENT_LIST_DIR}/NOTICE.md"
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/clipper2next
)

install(EXPORT clipper2next-targets
  NAMESPACE clipper2next::
  FILE clipper2nextTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clipper2next
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/clipper2nextConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/cmake/clipper2nextConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/clipper2nextConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clipper2next
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/clipper2nextConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/clipper2nextConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clipper2next
)
