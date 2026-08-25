#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "clipper2next/geometry/algorithms.h"

namespace {

namespace oracle = clipper2next::tests::oracle;

const auto zigzag_path = clipper2next::benchmarks::make_next_path(
    {0, 0, 20, 1, 40, 0, 60, 30, 80, 60, 100, 61, 120, 60, 140, 80, 160, 100});
const auto collinear_path = clipper2next::benchmarks::make_next_path(
    {0, 0, 50, 0, 100, 0, 100, 50, 100, 100, 0, 100, 0, 50});
const auto legacy_path = oracle::to_legacy_path(zigzag_path);
const auto legacy_collinear_path = oracle::to_legacy_path(collinear_path);

auto BM_legacy_trim_collinear(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::TrimCollinear(legacy_collinear_path, false);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_trim_collinear(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::trim_collinear(collinear_path, false);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_simplify_path(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::SimplifyPath(legacy_path, 4.0, false);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_simplify_path(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::simplify_path(zigzag_path, 4.0, false);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_ramer_douglas_peucker(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::RamerDouglasPeucker(legacy_path, 5.0);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_ramer_douglas_peucker(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::ramer_douglas_peucker(zigzag_path, 5.0);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_ellipse(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::Ellipse(
            clipper2next::benchmarks::legacy::Point64{100, 120}, 80.0, 40.0, 32U);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_ellipse(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::make_ellipse(
            clipper2next::benchmarks::next::Point64{100, 120}, 80.0, 40.0, 32U);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_legacy_trim_collinear);
BENCHMARK(BM_next_trim_collinear);
BENCHMARK(BM_legacy_simplify_path);
BENCHMARK(BM_next_simplify_path);
BENCHMARK(BM_legacy_ramer_douglas_peucker);
BENCHMARK(BM_next_ramer_douglas_peucker);
BENCHMARK(BM_legacy_ellipse);
BENCHMARK(BM_next_ellipse);

}  // namespace
