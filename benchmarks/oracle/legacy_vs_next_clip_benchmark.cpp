#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "clipper2next/clip.h"

namespace {

const auto fixture = clipper2next::benchmarks::make_clip_fixture();

auto to_legacy_clip_type(clipper2next::benchmarks::next::ClipType clip_type)
    -> clipper2next::benchmarks::legacy::ClipType {
    switch (clip_type) {
    case clipper2next::benchmarks::next::ClipType::NoClip: {
        return clipper2next::benchmarks::legacy::ClipType::NoClip;
    }
    case clipper2next::benchmarks::next::ClipType::Intersection: {
        return clipper2next::benchmarks::legacy::ClipType::Intersection;
    }
    case clipper2next::benchmarks::next::ClipType::Union: {
        return clipper2next::benchmarks::legacy::ClipType::Union;
    }
    case clipper2next::benchmarks::next::ClipType::Difference: {
        return clipper2next::benchmarks::legacy::ClipType::Difference;
    }
    case clipper2next::benchmarks::next::ClipType::Xor: {
        return clipper2next::benchmarks::legacy::ClipType::Xor;
    }
    }
    return clipper2next::benchmarks::legacy::ClipType::NoClip;
}

auto make_next_request(clipper2next::benchmarks::next::ClipType clip_type)
    -> clipper2next::benchmarks::next::clip_request64 {
    clipper2next::benchmarks::next::clip_request64 request;
    request.clip_type = clip_type;
    request.fill_rule = clipper2next::benchmarks::next::FillRule::NonZero;
    request.subjects = fixture.next_subject;
    request.clips = fixture.next_clip;
    return request;
}

const auto next_intersection_request =
    make_next_request(clipper2next::benchmarks::next::ClipType::Intersection);
const auto next_union_request = make_next_request(clipper2next::benchmarks::next::ClipType::Union);
const auto next_difference_request =
    make_next_request(clipper2next::benchmarks::next::ClipType::Difference);
const auto next_xor_request = make_next_request(clipper2next::benchmarks::next::ClipType::Xor);

auto execute_legacy(clipper2next::benchmarks::next::ClipType clip_type)
    -> clipper2next::benchmarks::legacy::Paths64 {
    clipper2next::benchmarks::legacy::Clipper64 clipper;
    clipper.AddSubject(fixture.legacy_subject);
    clipper.AddClip(fixture.legacy_clip);
    clipper2next::benchmarks::legacy::Paths64 result;
    clipper.Execute(to_legacy_clip_type(clip_type),
                    clipper2next::benchmarks::legacy::FillRule::NonZero,
                    result);
    return result;
}

auto execute_next(const clipper2next::benchmarks::next::clip_request64& request)
    -> clipper2next::benchmarks::next::Paths64 {
    return clipper2next::benchmarks::next::clip(request).closed;
}

auto verify_equivalence_once(clipper2next::benchmarks::next::ClipType clip_type,
                             const clipper2next::benchmarks::next::clip_request64& request)
    -> void {
    const auto legacy_result = execute_legacy(clip_type);
    const auto next_result = execute_next(request);
    clipper2next::benchmarks::assert_same_paths(legacy_result, next_result);
}

auto BM_legacy_clip_intersection(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Intersection,
                            next_intersection_request);
    for (auto _ : state) {
        auto result = execute_legacy(clipper2next::benchmarks::next::ClipType::Intersection);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_clip_intersection(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Intersection,
                            next_intersection_request);
    for (auto _ : state) {
        auto result = execute_next(next_intersection_request);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_clip_union(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Union, next_union_request);
    for (auto _ : state) {
        auto result = execute_legacy(clipper2next::benchmarks::next::ClipType::Union);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_clip_union(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Union, next_union_request);
    for (auto _ : state) {
        auto result = execute_next(next_union_request);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_clip_difference(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Difference,
                            next_difference_request);
    for (auto _ : state) {
        auto result = execute_legacy(clipper2next::benchmarks::next::ClipType::Difference);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_clip_difference(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Difference,
                            next_difference_request);
    for (auto _ : state) {
        auto result = execute_next(next_difference_request);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_clip_xor(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Xor, next_xor_request);
    for (auto _ : state) {
        auto result = execute_legacy(clipper2next::benchmarks::next::ClipType::Xor);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_clip_xor(benchmark::State& state) -> void {
    verify_equivalence_once(clipper2next::benchmarks::next::ClipType::Xor, next_xor_request);
    for (auto _ : state) {
        auto result = execute_next(next_xor_request);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_legacy_clip_intersection);
BENCHMARK(BM_next_clip_intersection);
BENCHMARK(BM_legacy_clip_union);
BENCHMARK(BM_next_clip_union);
BENCHMARK(BM_legacy_clip_difference);
BENCHMARK(BM_next_clip_difference);
BENCHMARK(BM_legacy_clip_xor);
BENCHMARK(BM_next_clip_xor);

}  // namespace
