include(FetchContent)

set(clipper2next_benchmark_common_libs
  benchmark::benchmark
  benchmark::benchmark_main
  clipper2next::clipper2next
)

if(NOT TARGET benchmark::benchmark)
  if(CLIPPER2NEXT_FETCH_DEPS)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googlebenchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG v1.9.5
    )
    FetchContent_MakeAvailable(googlebenchmark)
  else()
    find_package(benchmark CONFIG REQUIRED)
  endif()
endif()

function(clipper2next_add_benchmark target_name source_file)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name} PRIVATE ${clipper2next_benchmark_common_libs})
  target_compile_features(${target_name} PRIVATE cxx_std_23)
  clipper2next_target_warnings(${target_name})
  clipper2next_target_sanitizers(${target_name})
endfunction()

clipper2next_add_benchmark(
  clipper2next_bench_batch_parallel
  product/batch_parallel_benchmark.cpp)
clipper2next_add_benchmark(
  clipper2next_bench_product_clip
  product/product_clip_benchmark.cpp)
clipper2next_add_benchmark(
  clipper2next_bench_product_borrowed_topology
  product/product_borrowed_topology_benchmark.cpp)
clipper2next_add_benchmark(
  clipper2next_bench_product_offset
  product/product_offset_benchmark.cpp)
clipper2next_add_benchmark(
  clipper2next_bench_executor_offset
  product/executor_offset_benchmark.cpp)
clipper2next_add_benchmark(
  clipper2next_bench_product_rectclip
  product/product_rectclip_benchmark.cpp)
clipper2next_add_benchmark(
  clipper2next_bench_product_triangulation
  product/product_triangulation_benchmark.cpp)
clipper2next_add_benchmark(
  clipper2next_bench_product_transform
  product/product_transform_benchmark.cpp)

add_custom_target(clipper2next_product_benchmarks
  DEPENDS
    clipper2next_bench_batch_parallel
    clipper2next_bench_product_borrowed_topology
    clipper2next_bench_product_clip
    clipper2next_bench_product_offset
    clipper2next_bench_executor_offset
    clipper2next_bench_product_rectclip
    clipper2next_bench_product_triangulation
    clipper2next_bench_product_transform)

add_custom_target(clipper2next_benchmarks
  DEPENDS
    clipper2next_product_benchmarks)

if(CLIPPER2NEXT_BUILD_ORACLE)
    set(clipper2next_oracle_benchmark_libs
      ${clipper2next_benchmark_common_libs}
      ${clipper2next_oracle_legacy_target}
    )

    function(clipper2next_add_oracle_benchmark target_name source_file)
      add_executable(${target_name} ${source_file})
      target_link_libraries(${target_name} PRIVATE ${clipper2next_oracle_benchmark_libs})
      target_compile_features(${target_name} PRIVATE cxx_std_23)
      clipper2next_target_warnings(${target_name})
      clipper2next_target_sanitizers(${target_name})
    endfunction()

    function(clipper2next_add_external_corpus_benchmark target_name source_file)
      add_executable(${target_name} ${source_file})
      target_link_libraries(${target_name}
        PRIVATE
          benchmark::benchmark
          clipper2next::clipper2next
          ${clipper2next_oracle_legacy_target})
      target_compile_features(${target_name} PRIVATE cxx_std_23)
      clipper2next_target_warnings(${target_name})
      clipper2next_target_sanitizers(${target_name})
    endfunction()

    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_clip
      oracle/legacy_vs_next_clip_benchmark.cpp)
    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_offset
      oracle/legacy_vs_next_offset_benchmark.cpp)
    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_rectclip
      oracle/legacy_vs_next_rectclip_benchmark.cpp)
    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_geometry_algorithms
      oracle/legacy_vs_next_geometry_algorithms_benchmark.cpp)
    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_minkowski
      oracle/legacy_vs_next_minkowski_benchmark.cpp)
    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_polytree
      oracle/legacy_vs_next_polytree_benchmark.cpp)
    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_triangulation
      oracle/legacy_vs_next_triangulation_benchmark.cpp)
    clipper2next_add_oracle_benchmark(
      clipper2next_bench_legacy_vs_next_transform
      oracle/legacy_vs_next_transform_benchmark.cpp)
    clipper2next_add_external_corpus_benchmark(
      clipper2next_bench_external_corpus
      oracle/external_corpus_benchmark.cpp)
    clipper2next_target_msvc_external_pgo(
      clipper2next_bench_external_corpus BENCHMARK)

    set(clipper2next_oracle_benchmark_targets
      clipper2next_bench_legacy_vs_next_clip
      clipper2next_bench_legacy_vs_next_offset
      clipper2next_bench_legacy_vs_next_rectclip
      clipper2next_bench_legacy_vs_next_geometry_algorithms
      clipper2next_bench_legacy_vs_next_minkowski
      clipper2next_bench_legacy_vs_next_polytree
      clipper2next_bench_legacy_vs_next_triangulation
      clipper2next_bench_legacy_vs_next_transform
      clipper2next_bench_external_corpus)
  add_custom_target(clipper2next_oracle_benchmarks
    DEPENDS ${clipper2next_oracle_benchmark_targets})
endif()
