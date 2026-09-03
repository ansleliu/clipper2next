#include "clipper2next/offset/operations.h"
#include "api/private/borrowed_offset_execution.h"
#include "support/private/engine_resource_plan.h"
#include "clip/private/borrowed_topology_access.h"
#include "clip/private/clip_request_validation.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_group_processor.h"
#include "offset/private/offset_thread_state.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>
namespace clipper2next::internal {

auto execute_borrowed_offset_stage(
    const borrowed_offset_request64& request,
    const sync_bulk_executor_ref executor)
    -> expected_borrowed_offset_stage_result64 {
    auto result = borrowed_offset_stage_result64{};
    auto& stats = result.stats;
    if (const auto error = borrowed_paths64_access::path_count(request.paths,
                                                               stats.input_path_count);
        error != clipper_error_code::ok) {
        return make_clipper_error<borrowed_offset_stage_result64>(error);
    }
    if (stats.input_path_count > request.limits.maximum_input_path_count) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    }

    auto measurements =
        std::vector<path_source_contract::borrowed_path_measurement64>{};
    const auto measurement_capacity = measurements.capacity();
    measurements.resize(stats.input_path_count);
    if (measurements.capacity() != measurement_capacity) {
        ++stats.staging_reallocation_count;
    }
    auto maximum_path_point_count = std::size_t{};
    for (std::size_t index = 0; index < measurements.size(); ++index) {
        if (const auto error = borrowed_paths64_access::measure_path(
                request.paths, index, measurements[index]);
            error != clipper_error_code::ok) {
            return make_clipper_error<borrowed_offset_stage_result64>(error);
        }
        const auto count = measurements[index].source_point_count;
        maximum_path_point_count = std::max(
            maximum_path_point_count, count);
        if (stats.input_point_count > request.limits.maximum_input_point_count ||
            count > request.limits.maximum_input_point_count - stats.input_point_count) {
            return make_clipper_error<borrowed_offset_stage_result64>(
                clipper_error_code::resource_limit);
        }
        stats.input_point_count += count;
    }
    const auto effective_concurrency = executor.has_parallel_capability()
        ? std::min(
              executor.concurrency_limit(),
              internal::offset_parallel_maximum_concurrency)
        : 1U;
    const auto engine_plan = internal::plan_offset_generation_resources(
        stats.input_path_count, stats.input_point_count,
        maximum_path_point_count, request.delta, request.join_type,
        request.end_type, request.arc_tolerance,
        request.arc_segments_per_quadrant,
        effective_concurrency);
    if (engine_plan.work > request.limits.maximum_engine_work ||
        engine_plan.workspace_bytes >
            request.limits.maximum_engine_workspace_bytes) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    }
    auto engine_resources = internal::offset_engine_resource_context{
        .generation = engine_plan,
        .selected = engine_plan,
        .maximum_work = request.limits.maximum_engine_work,
        .maximum_workspace_bytes =
            request.limits.maximum_engine_workspace_bytes,
    };

    auto input_workspace_bytes = std::size_t{};
    if (const auto error = internal::measure_offset_path_storage(
            stats.input_path_count,
            stats.input_point_count,
            sizeof(internal::offset_path_record),
            input_workspace_bytes);
        error != clipper_error_code::ok) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    }
    auto measurement_workspace_bytes = std::size_t{};
    if (measurements.size() >
        (std::numeric_limits<std::size_t>::max)() /
            sizeof(path_source_contract::borrowed_path_measurement64)) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    }
    measurement_workspace_bytes =
        measurements.size() *
        sizeof(path_source_contract::borrowed_path_measurement64);
    if (internal::add_workspace_bytes(
            input_workspace_bytes,
            measurement_workspace_bytes,
            input_workspace_bytes) != clipper_error_code::ok ||
        input_workspace_bytes > request.limits.maximum_staging_workspace_bytes) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    }
    stats.peak_workspace_bytes = input_workspace_bytes;

    auto current_path_count = std::size_t{};
    if (const auto error = borrowed_paths64_access::path_count(
            request.paths, current_path_count);
        error != clipper_error_code::ok) {
        return make_clipper_error<borrowed_offset_stage_result64>(error);
    }
    if (current_path_count != measurements.size()) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::input_changed);
    }

    auto flat_paths = internal::flat_offset_paths64{};
    const auto record_capacity = flat_paths.paths.capacity();
    flat_paths.paths.reserve(measurements.size());
    if (flat_paths.paths.capacity() != record_capacity) {
        ++stats.staging_reallocation_count;
    }
    const auto point_capacity = flat_paths.points.capacity();
    flat_paths.points.reserve(stats.input_point_count);
    if (flat_paths.points.capacity() != point_capacity) {
        ++stats.staging_reallocation_count;
    }
    for (std::size_t index = 0; index < measurements.size(); ++index) {
        const auto& measurement = measurements[index];
        const auto point_offset = flat_paths.points.size();
        flat_paths.points.resize(point_offset + measurement.source_point_count);
        auto normalized_count = std::size_t{};
        auto point_write_count = std::size_t{};
        const auto error = borrowed_paths64_access::copy_path(
            request.paths,
            index,
            flat_paths.points.data() + point_offset,
            sizeof(Point64),
            measurement.source_point_count,
            measurement.normalized_point_count,
            normalized_count,
            point_write_count);
        if (error != clipper_error_code::ok) {
            return make_clipper_error<borrowed_offset_stage_result64>(error);
        }
        flat_paths.points.resize(point_offset + normalized_count);
        const auto copied = std::span<const Point64>{flat_paths.points}.subspan(
            point_offset, normalized_count);
        for (const auto& point : copied) {
            if (!internal::clip_coordinate_in_range(point.x) ||
                !internal::clip_coordinate_in_range(point.y)) {
                return make_clipper_error<borrowed_offset_stage_result64>(
                    clipper_error_code::coordinate_range);
            }
        }
        if (point_write_count >
            (std::numeric_limits<std::size_t>::max)() -
                stats.engine_input_point_writes) {
            return make_clipper_error<borrowed_offset_stage_result64>(
                clipper_error_code::resource_limit);
        }
        stats.engine_input_point_writes += point_write_count;
        flat_paths.paths.push_back(internal::offset_path_record{
            .point_offset = point_offset,
            .point_count = normalized_count,
        });
    }

    auto groups = std::vector<internal::offset_group>{};
    if (!flat_paths.paths.empty()) {
        groups.emplace_back(
            std::move(flat_paths), request.join_type, request.end_type);
    }
    auto& state = internal::acquire_reusable_offset_state();
    internal::execute_offset_algorithm(
        state,
        groups,
        request.delta,
        result.paths,
        internal::offset_algorithm_options{
            .miter_limit = request.miter_limit,
            .arc_tolerance = request.arc_tolerance,
            .arc_segments_per_quadrant = request.arc_segments_per_quadrant,
            .preserve_collinear = request.options.preserve_collinear,
            .reverse_solution = request.options.reverse_solution,
            .check_input_coordinate_range = false,
            .coordinate_rounding = request.coordinate_rounding,
        },
        nullptr,
        executor,
        &engine_resources,
        &stats.output_is_disjoint_simple_shells);

    stats.planned_engine_work = engine_resources.selected.work;
    stats.planned_engine_workspace_bytes =
        engine_resources.selected.workspace_bytes;

    stats.output_path_count = result.paths.size();
    if (stats.output_path_count > request.limits.maximum_output_path_count) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    }
    for (const auto path : result.paths) {
        if (stats.output_point_count > request.limits.maximum_output_point_count ||
            path.size() >
                request.limits.maximum_output_point_count - stats.output_point_count) {
            return make_clipper_error<borrowed_offset_stage_result64>(
                clipper_error_code::resource_limit);
        }
        stats.output_point_count += path.size();
    }
    auto output_workspace_bytes = std::size_t{};
    if (const auto error = internal::measure_offset_path_storage(
            stats.output_path_count,
            stats.output_point_count,
            sizeof(geotypes::PathDescriptor),
            output_workspace_bytes);
        error != clipper_error_code::ok ||
        internal::add_workspace_bytes(
            input_workspace_bytes,
            output_workspace_bytes,
            stats.peak_workspace_bytes) != clipper_error_code::ok ||
        stats.peak_workspace_bytes > request.limits.maximum_staging_workspace_bytes) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    }
    return result;
}

}  // namespace clipper2next::internal
