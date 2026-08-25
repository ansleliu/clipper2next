#include <benchmark/benchmark.h>

#include "clipper2next/triangulation.h"
#include "product_benchmark_fixtures.h"

namespace {

namespace product = clipper2next::benchmarks::product;

auto BM_product_triangulation_sweep(benchmark::State& state) -> void {
    const auto subjects =
        product::make_triangulation_subject(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        clipper2next::triangulation_request64 request;
        request.paths = subjects;
        request.use_delaunay = false;
        const auto result = clipper2next::triangulate(request);
        if (result.status != clipper2next::TriangulateResult::success) {
            state.SkipWithError("triangulation failed");
            break;
        }
        benchmark::DoNotOptimize(result.triangles.data());
        benchmark::DoNotOptimize(result.triangles.size());
    }
}

auto BM_product_triangulation_delaunay(benchmark::State& state) -> void {
    const auto subjects =
        product::make_triangulation_subject(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        clipper2next::triangulation_request64 request;
        request.paths = subjects;
        request.use_delaunay = true;
        const auto result = clipper2next::triangulate(request);
        if (result.status != clipper2next::TriangulateResult::success) {
            state.SkipWithError("triangulation failed");
            break;
        }
        benchmark::DoNotOptimize(result.triangles.data());
        benchmark::DoNotOptimize(result.triangles.size());
    }
}

BENCHMARK(BM_product_triangulation_sweep)->Arg(8)->Arg(32)->Arg(96);
BENCHMARK(BM_product_triangulation_delaunay)->Arg(8)->Arg(32)->Arg(96);

}  // namespace
