#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "clipper2next/rectclip.h"

namespace {

const auto fixture = clipper2next::benchmarks::make_rectclip_fixture();

auto make_next_rectclip_request() -> clipper2next::benchmarks::next::rect_clip_request64 {
    clipper2next::benchmarks::next::rect_clip_request64 request;
    request.rect = fixture.next_rect;
    request.paths = fixture.next_subject;
    return request;
}

const auto next_rectclip_request = make_next_rectclip_request();

auto execute_next_rectclip() -> clipper2next::benchmarks::next::Paths64 {
    return clipper2next::benchmarks::next::rect_clip(next_rectclip_request).paths;
}

auto verify_equivalence_once() -> void {
    const auto legacy_result =
        clipper2next::benchmarks::legacy::RectClip(fixture.legacy_rect, fixture.legacy_subject);
    const auto next_result = execute_next_rectclip();
    clipper2next::benchmarks::assert_same_paths(legacy_result, next_result);
}

auto BM_legacy_rectclip(benchmark::State& state) -> void {
    verify_equivalence_once();
    for (auto _ : state) {
        auto result =
            clipper2next::benchmarks::legacy::RectClip(fixture.legacy_rect, fixture.legacy_subject);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_rectclip(benchmark::State& state) -> void {
    verify_equivalence_once();
    for (auto _ : state) {
        auto result = execute_next_rectclip();
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_legacy_rectclip);
BENCHMARK(BM_next_rectclip);

}  // namespace
