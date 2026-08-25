#include <benchmark/benchmark.h>

#include "clipper2next/offset.h"
#include "product_benchmark_fixtures.h"

namespace {

namespace product = clipper2next::benchmarks::product;

auto execute_offset_request(const clipper2next::Paths64& paths,
                            double delta,
                            clipper2next::JoinType join_type,
                            clipper2next::EndType end_type,
                            double miter_limit = 2.0,
                            double arc_tolerance = 0.0)
    -> clipper2next::Paths64 {
    clipper2next::offset_request64 request;
    request.paths = paths;
    request.delta = delta;
    request.join_type = join_type;
    request.end_type = end_type;
    request.miter_limit = miter_limit;
    request.arc_tolerance = arc_tolerance;
    return clipper2next::offset(request).closed;
}

auto BM_product_offset_miter(benchmark::State& state) -> void {
    const auto subjects = product::make_offset_subjects(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        auto result = execute_offset_request(
            subjects, 18.0, clipper2next::JoinType::Miter, clipper2next::EndType::Polygon);
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(subjects.size()));
}

auto BM_product_offset_round(benchmark::State& state) -> void {
    const auto subjects = product::make_offset_subjects(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        auto result = execute_offset_request(subjects,
                                             18.0,
                                             clipper2next::JoinType::Round,
                                             clipper2next::EndType::Polygon,
                                             2.0,
                                             8U);
        benchmark::DoNotOptimize(result.data());
        benchmark::DoNotOptimize(result.size());
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(subjects.size()));
}

auto BM_product_borrowed_offset_flat(benchmark::State& state) -> void {
    const auto subjects =
        product::make_offset_subjects(static_cast<std::size_t>(state.range(0)));
    auto request = clipper2next::borrowed_offset_request64{};
    request.paths = clipper2next::borrow_paths64(subjects);
    request.delta = 18.0;
    request.join_type = clipper2next::JoinType::Miter;
    request.end_type = clipper2next::EndType::Polygon;

    for (auto _ : state) {
        auto result = clipper2next::offset_stage_checked(request);
        if (!result) {
            state.SkipWithError("borrowed offset failed");
            break;
        }
        benchmark::DoNotOptimize(result->paths.points().data());
        benchmark::DoNotOptimize(result->paths.point_count());
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<int64_t>(subjects.size()));
}

BENCHMARK(BM_product_offset_miter)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Arg(64);
BENCHMARK(BM_product_offset_round)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Arg(64);
BENCHMARK(BM_product_borrowed_offset_flat)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(64);

}  // namespace
