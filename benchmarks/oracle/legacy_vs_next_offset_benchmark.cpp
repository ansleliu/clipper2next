#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "clipper2next/offset.h"

namespace {

const auto fixture = clipper2next::benchmarks::make_offset_fixture();

auto make_next_offset_request() -> clipper2next::benchmarks::next::offset_request64 {
    clipper2next::benchmarks::next::offset_request64 request;
    request.paths = fixture.next_subject;
    request.delta = 25.0;
    request.join_type = clipper2next::benchmarks::next::JoinType::Miter;
    request.end_type = clipper2next::benchmarks::next::EndType::Polygon;
    return request;
}

const auto next_offset_request = make_next_offset_request();

auto execute_next_offset() -> clipper2next::benchmarks::next::Paths64 {
    return clipper2next::benchmarks::next::offset(next_offset_request).closed;
}

auto verify_equivalence_once() -> void {
    const auto legacy_result = clipper2next::benchmarks::legacy::InflatePaths(
        fixture.legacy_subject,
        25.0,
        clipper2next::benchmarks::legacy::JoinType::Miter,
        clipper2next::benchmarks::legacy::EndType::Polygon);
    const auto next_result = execute_next_offset();
    clipper2next::benchmarks::assert_same_paths(legacy_result, next_result);
}

auto BM_legacy_offset(benchmark::State& state) -> void {
    verify_equivalence_once();
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::InflatePaths(
            fixture.legacy_subject,
            25.0,
            clipper2next::benchmarks::legacy::JoinType::Miter,
            clipper2next::benchmarks::legacy::EndType::Polygon);
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }
}

auto BM_next_offset(benchmark::State& state) -> void {
    verify_equivalence_once();
    for (auto _ : state) {
        auto result = execute_next_offset();
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }
}

BENCHMARK(BM_legacy_offset);
BENCHMARK(BM_next_offset);

}  // namespace
