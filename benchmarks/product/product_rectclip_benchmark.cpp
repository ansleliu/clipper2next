#include <benchmark/benchmark.h>

#include "clipper2next/rectclip.h"
#include "product_benchmark_fixtures.h"

namespace {

namespace product = clipper2next::benchmarks::product;

auto BM_product_rectclip_polygons(benchmark::State& state) -> void {
    const clipper2next::Rect64 clip_rect{80, 70, 820, 620};
    const auto subjects = product::make_rectclip_subjects(static_cast<std::size_t>(state.range(0)));
    clipper2next::rect_clip_request64 request;
    request.rect = clip_rect;
    request.paths = subjects;

    for (auto _ : state) {
        auto result = clipper2next::rect_clip(request).paths;
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(subjects.size()));
}

auto BM_product_rectclip_lines(benchmark::State& state) -> void {
    const clipper2next::Rect64 clip_rect{80, 70, 820, 620};
    const auto subjects = product::make_rectclip_subjects(static_cast<std::size_t>(state.range(0)));
    clipper2next::rect_clip_lines_request64 request;
    request.rect = clip_rect;
    request.lines = subjects;

    for (auto _ : state) {
        auto result = clipper2next::rect_clip_lines(request).paths;
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(subjects.size()));
}

BENCHMARK(BM_product_rectclip_polygons)->Arg(24)->Arg(96)->Arg(192);
BENCHMARK(BM_product_rectclip_lines)->Arg(24)->Arg(96)->Arg(192);

}  // namespace
