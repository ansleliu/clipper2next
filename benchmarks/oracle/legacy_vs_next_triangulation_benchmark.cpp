#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "../product/product_benchmark_fixtures.h"

#include "clipper2/clipper.triangulation.h"
#include "clipper2next/triangulation.h"

#include <cstddef>

namespace {

namespace oracle = clipper2next::tests::oracle;
namespace product = clipper2next::benchmarks::product;
namespace legacy = clipper2next::benchmarks::legacy;
namespace next = clipper2next::benchmarks::next;

[[nodiscard]] auto make_next_subject(std::size_t point_count) -> next::Paths64 {
    return product::make_triangulation_subject(point_count);
}

[[nodiscard]] auto execute_legacy_triangulation(const legacy::Paths64& paths, bool use_delaunay)
    -> legacy::Paths64 {
    legacy::Paths64 triangles;
    const auto status = legacy::Triangulate(paths, triangles, use_delaunay);
    if (status != legacy::TriangulateResult::success) { return {}; }
    return triangles;
}

[[nodiscard]] auto execute_next_triangulation(const next::Paths64& paths, bool use_delaunay)
    -> next::Paths64 {
    next::triangulation_request64 request;
    request.paths = paths;
    request.use_delaunay = use_delaunay;
    const auto result = next::triangulate(request);
    if (result.status != next::TriangulateResult::success) { return {}; }
    return result.triangles;
}

auto BM_legacy_triangulation_sweep(benchmark::State& state) -> void {
    const auto subject = oracle::to_legacy_paths(make_next_subject(
        static_cast<std::size_t>(state.range(0))));

    for (auto _ : state) {
        auto result = execute_legacy_triangulation(subject, false);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_triangulation_sweep(benchmark::State& state) -> void {
    const auto subject = make_next_subject(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        auto result = execute_next_triangulation(subject, false);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_triangulation_delaunay(benchmark::State& state) -> void {
    const auto subject = oracle::to_legacy_paths(make_next_subject(
        static_cast<std::size_t>(state.range(0))));

    for (auto _ : state) {
        auto result = execute_legacy_triangulation(subject, true);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_triangulation_delaunay(benchmark::State& state) -> void {
    const auto subject = make_next_subject(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        auto result = execute_next_triangulation(subject, true);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_legacy_triangulation_sweep)->Arg(8)->Arg(32)->Arg(96);
BENCHMARK(BM_next_triangulation_sweep)->Arg(8)->Arg(32)->Arg(96);
BENCHMARK(BM_legacy_triangulation_delaunay)->Arg(8)->Arg(32)->Arg(96);
BENCHMARK(BM_next_triangulation_delaunay)->Arg(8)->Arg(32)->Arg(96);

}  // namespace
