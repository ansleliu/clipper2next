#include "clipper2next/offset/operations.h"
#include "api/private/borrowed_offset_execution.h"
#include "support/private/engine_resource_plan.h"
#include "clip/private/borrowed_topology_access.h"
#include "clip/private/clip_request_validation.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_geometry.h"
#include "offset/private/offset_group_processor.h"
#include "offset/private/offset_thread_state.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace {

auto require_success(const clipper_error_code error) -> void {
    if (error != clipper_error_code::ok) { raise_clipper_error(error); }
}

[[nodiscard]] auto bounded_sum(const std::size_t first,
                               const std::size_t second,
                               const std::size_t maximum) -> std::size_t {
    if (first > maximum || second > maximum - first) {
        raise_clipper_error(clipper_error_code::resource_limit);
    }
    return first + second;
}

[[nodiscard]] auto bounded_product(const std::size_t count,
                                   const std::size_t bytes,
                                   const std::size_t maximum) -> std::size_t {
    if (bytes != 0U && count > maximum / bytes) {
        raise_clipper_error(clipper_error_code::resource_limit);
    }
    return count * bytes;
}

struct collected_offset_group final {
    offset_group input;
    engine_resource_plan generation;
    std::size_t retained_bytes{};
};

[[nodiscard]] auto copy_group_paths(
    const borrowed_offset_group64& source,
    const std::span<const path_source_contract::borrowed_path_measurement64> measurements,
    const std::size_t point_count,
    borrowed_offset_stage_stats64& stats) -> flat_offset_paths64 {
    const auto path_count = measurements.size();
    const auto open_vertices = !is_closed_path(source.end_type);
    auto flat_paths = flat_offset_paths64{};
    flat_paths.paths.reserve(path_count);
    flat_paths.points.reserve(point_count);
    stats.staging_reallocation_count += path_count != 0U;
    stats.staging_reallocation_count += point_count != 0U;
    for (auto index = std::size_t{}; index < path_count; ++index) {
        const auto& measurement = measurements[index];
        const auto point_offset = flat_paths.points.size();
        flat_paths.points.resize(point_offset + measurement.source_point_count);
        auto normalized_count = std::size_t{};
        auto point_write_count = std::size_t{};
        require_success(borrowed_paths64_access::copy_path(source.paths,
                                                           index,
                                                           flat_paths.points.data() + point_offset,
                                                           sizeof(Point64),
                                                           measurement.source_point_count,
                                                           measurement.normalized_point_count,
                                                           normalized_count,
                                                           point_write_count,
                                                           open_vertices));
        // Stability is checked using the transport's normalized count. Open
        // operations retain their actual final vertex, even when it equals
        // the first one; this is the same input as the owning offset kernel.
        if (open_vertices) { normalized_count = point_write_count; }
        flat_paths.points.resize(point_offset + normalized_count);
        for (const auto& point :
             std::span<const Point64>{flat_paths.points}.subspan(point_offset, normalized_count)) {
            if (!clip_coordinate_in_range(point.x) || !clip_coordinate_in_range(point.y)) {
                raise_clipper_error(clipper_error_code::coordinate_range);
            }
        }
        stats.engine_input_point_writes = bounded_sum(stats.engine_input_point_writes,
                                                      point_write_count,
                                                      (std::numeric_limits<std::size_t>::max)());
        flat_paths.paths.push_back(offset_path_record{point_offset, normalized_count});
    }
    return flat_paths;
}

[[nodiscard]] auto collect_group(const borrowed_offset_group64& source,
                                 const borrowed_offset_request64& request,
                                 const std::size_t concurrency,
                                 const std::size_t retained_bytes,
                                 borrowed_offset_stage_stats64& stats) -> collected_offset_group {
    const auto& limits = request.limits;
    const auto open_vertices = !is_closed_path(source.end_type);
    auto path_count = std::size_t{};
    require_success(borrowed_paths64_access::path_count(source.paths, path_count));
    stats.input_path_count =
        bounded_sum(stats.input_path_count, path_count, limits.maximum_input_path_count);
    const auto measurement_bytes =
        bounded_product(path_count,
                        sizeof(path_source_contract::borrowed_path_measurement64),
                        limits.maximum_staging_workspace_bytes);
    const auto measurement_peak =
        bounded_sum(retained_bytes, measurement_bytes, limits.maximum_staging_workspace_bytes);
    stats.peak_workspace_bytes = std::max(stats.peak_workspace_bytes, measurement_peak);

    auto measurements = std::vector<path_source_contract::borrowed_path_measurement64>(path_count);
    stats.staging_reallocation_count += !measurements.empty();
    auto point_count = std::size_t{};
    auto maximum_path_point_count = std::size_t{};
    for (auto index = std::size_t{}; index < path_count; ++index) {
        require_success(borrowed_paths64_access::measure_path(
            source.paths, index, measurements[index], open_vertices));
        const auto count = measurements[index].source_point_count;
        stats.input_point_count =
            bounded_sum(stats.input_point_count, count, limits.maximum_input_point_count);
        point_count = bounded_sum(point_count, count, limits.maximum_input_point_count);
        maximum_path_point_count = std::max(maximum_path_point_count, count);
    }
    const auto generation = plan_offset_generation_resources(path_count,
                                                             point_count,
                                                             maximum_path_point_count,
                                                             request.delta,
                                                             source.join_type,
                                                             source.end_type,
                                                             request.arc_tolerance,
                                                             request.arc_segments_per_quadrant,
                                                             concurrency);
    if (generation.work > limits.maximum_engine_work ||
        generation.workspace_bytes > limits.maximum_engine_workspace_bytes) {
        raise_clipper_error(clipper_error_code::resource_limit);
    }
    auto input_bytes = std::size_t{};
    require_success(measure_offset_path_storage(
        path_count, point_count, sizeof(offset_path_record), input_bytes));
    const auto peak =
        bounded_sum(measurement_peak, input_bytes, limits.maximum_staging_workspace_bytes);
    stats.peak_workspace_bytes = std::max(stats.peak_workspace_bytes, peak);

    auto current_path_count = std::size_t{};
    require_success(borrowed_paths64_access::path_count(source.paths, current_path_count));
    if (current_path_count != path_count) {
        raise_clipper_error(clipper_error_code::input_changed);
    }

    auto flat_paths = copy_group_paths(source, measurements, point_count, stats);
    // The measurements die here, before generation/cleanup. Only one flat
    // point pool and one descriptor pool per semantic group remain live.
    return {offset_group{std::move(flat_paths), source.join_type, source.end_type},
            generation,
            input_bytes};
}

}  // namespace

auto execute_borrowed_offset_stage(const borrowed_offset_request64& request,
                                   const sync_bulk_executor_ref executor)
    -> expected_borrowed_offset_stage_result64 {
    auto result = borrowed_offset_stage_result64{};
    auto& stats = result.stats;
    const auto& limits = request.limits;
    const auto group_bytes = bounded_product(
        request.groups.size(), sizeof(offset_group), limits.maximum_staging_workspace_bytes);
    auto retained_bytes = group_bytes;
    stats.peak_workspace_bytes = group_bytes;
    auto groups = std::vector<offset_group>{};
    groups.reserve(request.groups.size());
    const auto concurrency =
        executor.has_parallel_capability()
            ? std::min(executor.concurrency_limit(), offset_parallel_maximum_concurrency)
            : 1U;
    auto generation = engine_resource_plan{};
    for (const auto& source : request.groups) {
        auto collected = collect_group(source, request, concurrency, retained_bytes, stats);
        generation.work =
            bounded_sum(generation.work, collected.generation.work, limits.maximum_engine_work);
        generation.workspace_bytes = bounded_sum(generation.workspace_bytes,
                                                 collected.generation.workspace_bytes,
                                                 limits.maximum_engine_workspace_bytes);
        retained_bytes = bounded_sum(
            retained_bytes, collected.retained_bytes, limits.maximum_staging_workspace_bytes);
        if (collected.input.path_count() != 0U) { groups.push_back(std::move(collected.input)); }
    }
    auto resources = offset_engine_resource_context{
        .generation = generation,
        .selected = generation,
        .maximum_work = limits.maximum_engine_work,
        .maximum_workspace_bytes = limits.maximum_engine_workspace_bytes};
    auto& state = acquire_reusable_offset_state();
    execute_offset_algorithm(
        state,
        groups,
        request.delta,
        result.paths,
        offset_algorithm_options{.miter_limit = request.miter_limit,
                                 .arc_tolerance = request.arc_tolerance,
                                 .arc_segments_per_quadrant = request.arc_segments_per_quadrant,
                                 .preserve_collinear = request.options.preserve_collinear,
                                 .reverse_solution = request.options.reverse_solution,
                                 .check_input_coordinate_range = false,
                                 .coordinate_rounding = request.coordinate_rounding,
                                 .intersection_policy = request.options.intersection_policy},
        nullptr,
        executor,
        &resources,
        &stats.output_is_disjoint_simple_shells);

    stats.planned_engine_work = resources.selected.work;
    stats.planned_engine_workspace_bytes = resources.selected.workspace_bytes;
    stats.output_path_count = result.paths.size();
    if (stats.output_path_count > limits.maximum_output_path_count) {
        raise_clipper_error(clipper_error_code::resource_limit);
    }
    for (const auto path : result.paths) {
        stats.output_point_count =
            bounded_sum(stats.output_point_count, path.size(), limits.maximum_output_point_count);
    }
    auto output_bytes = std::size_t{};
    require_success(measure_offset_path_storage(stats.output_path_count,
                                                stats.output_point_count,
                                                sizeof(geotypes::PathDescriptor),
                                                output_bytes));
    stats.peak_workspace_bytes =
        std::max(stats.peak_workspace_bytes,
                 bounded_sum(retained_bytes, output_bytes, limits.maximum_staging_workspace_bytes));
    return result;
}

}  // namespace clipper2next::internal
