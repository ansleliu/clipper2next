#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "clipper2next/clip.h"

namespace {

namespace legacy = clipper2next::benchmarks::legacy;
namespace next = clipper2next::benchmarks::next;
namespace oracle = clipper2next::tests::oracle;

auto make_nested_rectangles_legacy(std::size_t count) -> legacy::Paths64 {
    legacy::Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto inset = static_cast<int64_t>(index * 8U);
        paths.push_back(legacy::MakePath({inset,
                                          inset,
                                          10000 - inset,
                                          inset,
                                          10000 - inset,
                                          10000 - inset,
                                          inset,
                                          10000 - inset}));
    }
    return paths;
}

auto make_next_request(std::size_t count) -> next::clip_request64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = oracle::to_next_paths(make_nested_rectangles_legacy(count));
    return request;
}

auto make_legacy_subjects(std::size_t count) -> legacy::Paths64 {
    return make_nested_rectangles_legacy(count);
}

auto BM_legacy_polytree_union(benchmark::State& state) -> void {
    const auto subjects = make_legacy_subjects(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        legacy::Clipper64 clipper;
        clipper.AddSubject(subjects);
        legacy::PolyTree64 result;
        clipper.Execute(legacy::ClipType::Union, legacy::FillRule::NonZero, result);
        benchmark::DoNotOptimize(result.Count());
    }
}

auto BM_next_polytree_union(benchmark::State& state) -> void {
    const auto request = make_next_request(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        auto result = next::clip_tree(request);
        benchmark::DoNotOptimize(result.tree.count(result.tree.root()));
    }
}

BENCHMARK(BM_legacy_polytree_union)->Arg(8)->Arg(32)->Arg(96);
BENCHMARK(BM_next_polytree_union)->Arg(8)->Arg(32)->Arg(96);

}  // namespace
