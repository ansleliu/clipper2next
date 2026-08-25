#include <benchmark/benchmark.h>

#include "benchmark_fixtures.h"
#include "clipper2next/geometry/path_transforms.h"
#include "clipper2next/geometry/translate.h"
#include "clipper2next/geometry/scaling.h"

#include <cstddef>

namespace {

[[nodiscard]] auto make_transform_path(std::size_t count)
    -> clipper2next::benchmarks::next::Path64 {
    clipper2next::benchmarks::next::Path64 path;
    path.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        path.emplace_back(static_cast<int64_t>(index * 7U),
                          static_cast<int64_t>((index % 257U) * 11U - index));
    }
    return path;
}

const auto next_path = make_transform_path(4096U);
const auto legacy_path = clipper2next::tests::oracle::to_legacy_path(next_path);

auto BM_legacy_translate_path64(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::TranslatePath(
            legacy_path, int64_t{123}, int64_t{-456});
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_translate_path64(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result =
            clipper2next::benchmarks::next::translate(next_path, int64_t{123}, int64_t{-456});
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_transform_path64_to_pathd(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::TransformPath<double, int64_t>(legacy_path);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_transform_path64_to_pathd(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::transform_path<double>(next_path);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_legacy_scale_path64_to_pathd(benchmark::State& state) -> void {
    int error_code = 0;
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::legacy::ScalePath<double, int64_t>(
            legacy_path, 0.25, error_code);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_next_scale_path64_to_pathd(benchmark::State& state) -> void {
    for (auto _ : state) {
        auto result = clipper2next::benchmarks::next::scale_path<double, int64_t>(
            next_path, clipper2next::benchmarks::next::scale_request{0.25, 0.25});
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_legacy_translate_path64);
BENCHMARK(BM_next_translate_path64);
BENCHMARK(BM_legacy_transform_path64_to_pathd);
BENCHMARK(BM_next_transform_path64_to_pathd);
BENCHMARK(BM_legacy_scale_path64_to_pathd);
BENCHMARK(BM_next_scale_path64_to_pathd);

}  // namespace
