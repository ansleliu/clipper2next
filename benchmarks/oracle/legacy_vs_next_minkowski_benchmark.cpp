#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "clipper2next/minkowski.h"

namespace {

namespace oracle = clipper2next::tests::oracle;

const auto pattern =
    clipper2next::benchmarks::make_next_path({-30, -20, 30, -20, 50, 0, 30, 20, -30, 20, -50, 0});
const auto closed_path =
    clipper2next::benchmarks::make_next_path({0, 0, 200, 20, 260, 140, 160, 260, 20, 220});
const auto open_path =
    clipper2next::benchmarks::make_next_path({0, 0, 90, 25, 130, 120, 240, 150, 320, 80});

const auto legacy_pattern = oracle::to_legacy_path(pattern);
const auto legacy_closed_path = oracle::to_legacy_path(closed_path);
const auto legacy_open_path = oracle::to_legacy_path(open_path);

auto make_next_request(const clipper2next::benchmarks::next::Path64& path, bool is_closed)
    -> clipper2next::benchmarks::next::minkowski_request64 {
    clipper2next::benchmarks::next::minkowski_request64 request;
    request.pattern = pattern;
    request.path = path;
    request.is_closed = is_closed;
    return request;
}

const auto next_closed_request = make_next_request(closed_path, true);
const auto next_open_request = make_next_request(open_path, false);

auto BM_legacy_minkowski_sum_closed(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::MinkowskiSum(
            legacy_pattern, legacy_closed_path, true);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_minkowski_sum_closed(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::minkowski_sum(next_closed_request);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_minkowski_difference_open(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::MinkowskiDiff(
            legacy_pattern, legacy_open_path, false);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_minkowski_difference_open(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::minkowski_difference(next_open_request);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_legacy_minkowski_sum_closed);
BENCHMARK(BM_next_minkowski_sum_closed);
BENCHMARK(BM_legacy_minkowski_difference_open);
BENCHMARK(BM_next_minkowski_difference_open);

}  // namespace
