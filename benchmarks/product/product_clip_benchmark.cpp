#include <benchmark/benchmark.h>

#include "clipper2next/clip.h"
#include "product_benchmark_fixtures.h"

namespace {

namespace product = clipper2next::benchmarks::product;

auto execute_clip_request(clipper2next::ClipType clip_type,
                          const clipper2next::Paths64& subjects,
                          const clipper2next::Paths64& clips) -> clipper2next::Paths64 {
    clipper2next::clip_request64 request;
    request.clip_type = clip_type;
    request.fill_rule = clipper2next::FillRule::NonZero;
    request.subjects = subjects;
    request.clips = clips;
    return clipper2next::clip(request).closed;
}

auto BM_product_clip_union_grid(benchmark::State& state) -> void {
    const auto subjects = product::make_clip_subjects(static_cast<std::size_t>(state.range(0)));
    const auto clips = product::make_clip_windows(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        auto result = execute_clip_request(clipper2next::ClipType::Union, subjects, clips);
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(subjects.size()));
}

auto BM_product_clip_intersection_grid(benchmark::State& state) -> void {
    const auto subjects = product::make_clip_subjects(static_cast<std::size_t>(state.range(0)));
    const auto clips = product::make_clip_windows(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        auto result = execute_clip_request(clipper2next::ClipType::Intersection, subjects, clips);
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(subjects.size()));
}

BENCHMARK(BM_product_clip_union_grid)->Arg(1)->Arg(16)->Arg(64);
BENCHMARK(BM_product_clip_intersection_grid)->Arg(1)->Arg(16)->Arg(64);

}  // namespace
