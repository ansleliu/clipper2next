#include <benchmark/benchmark.h>

#include "clipper2next/clip.h"
#include "product_benchmark_fixtures.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

namespace next = clipper2next;
namespace product = clipper2next::benchmarks::product;

struct foreign_point64 final {
    std::int64_t x{};
    std::int64_t y{};
};

using foreign_path64 = std::vector<foreign_point64>;
using foreign_paths64 = std::vector<foreign_path64>;

struct flat_ring64 final {
    std::size_t polygon_index{};
    next::topology_ring_role role{next::topology_ring_role::shell};
    std::size_t point_offset{};
    std::size_t point_count{};
};

struct flat_topology64 final {
    auto begin(const next::topology_layout64& layout) -> next::clipper_error_code {
        polygons.assign(layout.polygons.begin(), layout.polygons.end());
        rings.clear();
        rings.reserve(layout.ring_count);
        points.resize(layout.point_count);
        point_offset = 0U;
        return next::clipper_error_code::ok;
    }

    auto acquire(const next::topology_ring_layout64& ring,
                 std::span<geotypes::Point2i64>& destination)
        -> next::clipper_error_code {
        if (ring.point_count > points.size() - point_offset) {
            destination = {};
            return next::clipper_error_code::sink_failure;
        }
        rings.push_back({ring.polygon_index, ring.role, point_offset, ring.point_count});
        destination = std::span{points}.subspan(point_offset, ring.point_count);
        point_offset += ring.point_count;
        return next::clipper_error_code::ok;
    }

    auto finish() -> next::clipper_error_code {
        return point_offset == points.size() ? next::clipper_error_code::ok
                                             : next::clipper_error_code::sink_failure;
    }

    auto cancel() noexcept -> void {
        polygons.clear();
        rings.clear();
        points.clear();
        point_offset = 0U;
    }

    std::vector<next::topology_polygon_layout64> polygons{};
    std::vector<flat_ring64> rings{};
    std::vector<geotypes::Point2i64> points{};
    std::size_t point_offset{};
};

[[nodiscard]] auto make_union_subjects(std::size_t count) -> next::Paths64 {
    auto subjects = product::make_clip_subjects(count);
    auto clips = product::make_clip_windows(count);
    subjects.reserve(subjects.size() + clips.size());
    for (auto& path : clips) { subjects.emplace_back(std::move(path)); }
    return subjects;
}

[[nodiscard]] auto make_foreign_paths(const next::Paths64& paths) -> foreign_paths64 {
    foreign_paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        auto& output = result.emplace_back();
        output.reserve(path.size());
        for (const auto& point : path) { output.push_back({point.x, point.y}); }
    }
    return result;
}

auto BM_product_owning_clip_tree_into_union(benchmark::State& state) -> void {
    auto request = next::clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = make_union_subjects(static_cast<std::size_t>(state.range(0)));
    auto result = next::clip_tree64_result{};

    for (auto _ : state) {
        next::clip_tree_into(request, result);
        benchmark::DoNotOptimize(result.tree.count(result.tree.root()));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(request.subjects.size()));
}

auto BM_product_borrowed_topology_union(benchmark::State& state) -> void {
    const auto owning_subjects =
        make_union_subjects(static_cast<std::size_t>(state.range(0)));
    const auto subjects = make_foreign_paths(owning_subjects);
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::borrow_paths64(subjects);
    auto output = flat_topology64{};
    auto last_stats = next::topology_write_stats64{};

    for (auto _ : state) {
        const auto result =
            next::clip_topology_checked(request, next::make_topology_writer64(output));
        if (!result.has_value()) {
            state.SkipWithError(next::clipper_error_message(result.error()));
            break;
        }
        last_stats = *result;
        benchmark::DoNotOptimize(output.points.data());
        benchmark::DoNotOptimize(output.rings.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(subjects.size()));
    state.counters["input_collection_point_writes"] =
        static_cast<double>(last_stats.input_collection_point_writes);
    state.counters["engine_input_point_writes"] =
        static_cast<double>(last_stats.engine_input_point_writes);
    state.counters["output_ring_acquire_count"] =
        static_cast<double>(last_stats.output_ring_acquire_count);
    state.counters["output_final_point_writes"] =
        static_cast<double>(last_stats.output_final_point_writes);
    state.counters["staging_reallocations"] =
        static_cast<double>(last_stats.staging_reallocation_count);
}

BENCHMARK(BM_product_owning_clip_tree_into_union)->Arg(16)->Arg(64)->Arg(256);
BENCHMARK(BM_product_borrowed_topology_union)->Arg(16)->Arg(64)->Arg(256);

}  // namespace
