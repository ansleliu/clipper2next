#include <benchmark/benchmark.h>

#include "clipper2next/geometry/path_transforms.h"
#include "clipper2next/geometry/translate.h"
#include "clipper2next/geometry/scaling.h"

#include <cstddef>

namespace {

auto make_path64(std::size_t point_count) -> clipper2next::Path64 {
    clipper2next::Path64 path;
    path.reserve(point_count);
    for (std::size_t index = 0; index < point_count; ++index) {
        const auto x = static_cast<int64_t>(index * 17U);
        const auto y = static_cast<int64_t>((index % 97U) * 31U + (index / 97U) * 13U);
        path.emplace_back(x, y);
    }
    return path;
}

auto make_pathd(std::size_t point_count) -> clipper2next::PathD {
    clipper2next::PathD path;
    path.reserve(point_count);
    for (std::size_t index = 0; index < point_count; ++index) {
        const auto value = static_cast<double>(index);
        path.emplace_back(value * 0.25, value * 0.125);
    }
    return path;
}

auto BM_transform_path64_to_pathd(benchmark::State& state) -> void {
    const auto path = make_path64(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        auto result = clipper2next::transform_path<double, int64_t>(path);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_transform_pathd_to_path64(benchmark::State& state) -> void {
    const auto path = make_pathd(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        auto result = clipper2next::transform_path<int64_t, double>(path);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_translate_path64(benchmark::State& state) -> void {
    const auto path = make_path64(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        auto result = clipper2next::translate(path, 17, -23);
        benchmark::DoNotOptimize(result);
    }
}

auto BM_scale_path64_to_pathd(benchmark::State& state) -> void {
    const auto path = make_path64(static_cast<std::size_t>(state.range(0)));
    const clipper2next::scale_request request{0.125, 0.25};
    for (auto _ : state) {
        auto result = clipper2next::scale_path<double, int64_t>(path, request);
        benchmark::DoNotOptimize(result.value());
    }
}

BENCHMARK(BM_transform_path64_to_pathd)->Arg(128)->Arg(4096)->Arg(65536);
BENCHMARK(BM_transform_pathd_to_path64)->Arg(128)->Arg(4096)->Arg(65536);
BENCHMARK(BM_translate_path64)->Arg(128)->Arg(4096)->Arg(65536);
BENCHMARK(BM_scale_path64_to_pathd)->Arg(128)->Arg(4096)->Arg(65536);

}  // namespace
