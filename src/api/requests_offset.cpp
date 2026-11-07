#include "clipper2next/offset/operations.h"
#include "api/private/borrowed_offset_execution.h"
#include "support/private/engine_resource_plan.h"
#include "clip/private/borrowed_topology_access.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_geometry.h"
#include "offset/private/offset_group_processor.h"
#include "offset/private/offset_thread_state.h"

#include "support/private/checked_size.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>
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

struct collected_offset_group final {
    flat_offset_paths64 input;
    engine_resource_plan generation;
};

auto copy_group_paths_into(
    const borrowed_offset_group64& source,
    const std::span<const path_source_contract::borrowed_path_measurement64> measurements,
    const std::size_t point_count,
    borrowed_offset_stage_stats64& stats,
    scoped_workspace_charge& input_charge, flat_offset_paths64& flat_paths) -> void {
    const auto path_count = measurements.size();
    const auto open_vertices = !is_closed_path(source.end_type);
    flat_paths.paths.reserve(path_count);
    flat_paths.points.reserve(point_count);
    stats.staging_reallocation_count += path_count != 0U;
    stats.staging_reallocation_count += point_count != 0U;
    for (auto index = std::size_t{}; index < path_count; ++index) {
        const auto& measurement = measurements[index];
        const auto point_offset = flat_paths.points.size();
        const auto next_point_count = checked_size_add(point_offset, measurement.source_point_count);
        assert(next_point_count <= flat_paths.points.capacity());
        // The complete measured group was reserved above. Constructed points
        // must be live before source callbacks, but this growth cannot call out.
        require_success(input_charge.try_construct_bytes<workspace_category::staging>(
            checked_size_add(input_charge.bytes(),
                             checked_size_multiply(measurement.source_point_count, sizeof(Point64))),
            [&]() noexcept { flat_paths.points.resize(next_point_count); }));
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
        if (normalized_count != measurement.source_point_count) {
            flat_paths.points.resize(point_offset + normalized_count);
            input_charge.set_live_slots(input_charge.bytes() - (measurement.source_point_count - normalized_count) * sizeof(Point64), 1U);
        }
        const auto next_input_bytes = checked_size_add(input_charge.bytes(), sizeof(offset_path_record));
        assert(flat_paths.paths.size() < flat_paths.paths.capacity());
        require_success(input_charge.try_construct_bytes<workspace_category::staging>(
            next_input_bytes, [&]() noexcept {
                flat_paths.paths.push_back(offset_path_record{point_offset, normalized_count});
            }));
        assert(point_write_count <= stats.input_point_count - stats.engine_input_point_writes);
        stats.engine_input_point_writes += point_write_count;
    }
}

[[nodiscard]] auto collect_group(const borrowed_offset_group64& source,
                                 const borrowed_offset_request64& request,
                                 const std::size_t concurrency,
                                 borrowed_offset_stage_stats64& stats,
                                 offset_engine_resource_context& resources,
                                 scoped_workspace_charge& input_charge) -> collected_offset_group {
    const auto& limits = request.limits;
    const auto open_vertices = !is_closed_path(source.end_type);
    auto path_count = std::size_t{};
    require_success(borrowed_paths64_access::path_count(source.paths, path_count));
    stats.input_path_count =
        bounded_sum(stats.input_path_count, path_count, limits.maximum_input_path_count);
    auto measurement_charge = scoped_workspace_charge{&resources, workspace_category::staging};
    auto measurement_growth = scoped_workspace_growth{measurement_charge, path_count, sizeof(path_source_contract::borrowed_path_measurement64)};

    using measurement_type = path_source_contract::borrowed_path_measurement64;
    auto single_measurement = std::optional<measurement_type>{};
    auto allocated_measurements = std::vector<measurement_type>(path_count == 1U ? 0U : path_count);
    auto measurements = std::span<measurement_type>{allocated_measurements};
    if (path_count == 1U) { measurements = {&single_measurement.emplace(), 1U}; }
    measurement_growth.commit();
    stats.staging_reallocation_count += !allocated_measurements.empty();
    auto point_count = std::size_t{};
    auto maximum_path_point_count = std::size_t{};
    for (auto index = std::size_t{}; index < path_count; ++index) {
        require_success(borrowed_paths64_access::measure_path(
            source.paths, index, measurements[index],
            limits.maximum_input_point_count - stats.input_point_count, open_vertices));
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
    if (generation.work == (std::numeric_limits<std::size_t>::max)() ||
        generation.workspace_bytes == (std::numeric_limits<std::size_t>::max)() ||
        generation.work > limits.maximum_engine_work ||
        generation.workspace_bytes > limits.maximum_engine_workspace_bytes) {
        raise_clipper_error(clipper_error_code::resource_limit);
    }
    auto current_path_count = std::size_t{};
    require_success(borrowed_paths64_access::path_count(source.paths, current_path_count));
    if (current_path_count != path_count) {
        raise_clipper_error(clipper_error_code::input_changed);
    }

    auto result = collected_offset_group{flat_offset_paths64{}, generation};
    copy_group_paths_into(source, measurements, point_count, stats, input_charge, result.input);
    // The measurements die here, before generation/cleanup. Only one flat
    // point pool and one descriptor pool per semantic group remain live.
    return result;
}

}  // namespace

auto execute_borrowed_offset_stage(const borrowed_offset_request64& request,
                                   const sync_bulk_executor_ref executor,
                                   offset_engine_resource_context& resources)
    -> expected_borrowed_offset_stage_result64 {
    auto lease = offset_state_lease{};
    auto result = expected_borrowed_offset_stage_result64{std::in_place};
    auto& stats = result->stats;
    const auto& limits = request.limits;
    // The entry point owns a fresh operation ledger. Reused diagnostic ledgers
    // are reset by their caller before starting another operation.
    assert(resources.work == 0U && resources.engine_high_water == 0U &&
           resources.staging_high_water == 0U);
    resources.maximum_output_paths = limits.maximum_output_path_count;
    resources.maximum_output_points = limits.maximum_output_point_count;
    resources.maximum_work = limits.maximum_engine_work;
    resources.maximum_workspace_bytes = limits.maximum_engine_workspace_bytes;
    resources.maximum_staging_bytes = limits.maximum_staging_workspace_bytes;
    auto group_charge = scoped_workspace_charge{&resources, workspace_category::staging};
    auto input_charge = scoped_workspace_charge{&resources, workspace_category::staging};
    auto allocated_groups = std::vector<offset_group>{};
    allocated_groups.reserve(request.groups.size() == 1U ? 0U : request.groups.size());
    const auto concurrency =
        executor.has_parallel_capability()
            ? std::min(executor.concurrency_limit(), offset_parallel_maximum_concurrency)
            : 1U;
    auto generation = engine_resource_plan{};
    const auto finish = [&](const std::span<const offset_group> groups) {
        resources.generation = generation;
        resources.selected = generation;
        require_success(resources.try_add_work(generation.work));
        if (stats.input_point_count == 0U) {
            stats.peak_workspace_bytes = resources.staging_high_water;
            return;
        }
        auto& state = lease.state();
        auto output_charge = execute_offset_algorithm(
            state,
            groups,
            request.delta,
            result->paths,
            offset_algorithm_options{.miter_limit = request.miter_limit,
                                     .arc_tolerance = request.arc_tolerance,
                                     .arc_segments_per_quadrant = request.arc_segments_per_quadrant,
                                     .preserve_collinear = request.options.preserve_collinear,
                                     .reverse_solution = request.options.reverse_solution,
                                     .coordinate_rounding = request.coordinate_rounding,
                                     .intersection_policy = request.options.intersection_policy},
            nullptr,
            executor,
            &resources,
            &stats.output_is_disjoint_simple_shells);

        resources.selected.work = resources.work;
        resources.selected.workspace_bytes = std::max(resources.selected.workspace_bytes, resources.engine_high_water);
        stats.planned_engine_work = resources.work;
        stats.planned_engine_workspace_bytes = resources.selected.workspace_bytes;
        stats.output_path_count = result->paths.size();
        if (stats.output_path_count > limits.maximum_output_path_count) {
            raise_clipper_error(clipper_error_code::resource_limit);
        }
        for (const auto path : result->paths) {
            stats.output_point_count =
                bounded_sum(stats.output_point_count, path.size(), limits.maximum_output_point_count);
        }
        stats.peak_workspace_bytes = resources.staging_high_water;
    };
    for (const auto& source : request.groups) {
        auto collected = collect_group(source, request, concurrency, stats, resources, input_charge);
        generation.work =
            bounded_sum(generation.work, collected.generation.work, limits.maximum_engine_work);
        generation.workspace_bytes = std::max(generation.workspace_bytes,
            bounded_sum(generation.retained_output_bytes, collected.generation.workspace_bytes,
                        limits.maximum_engine_workspace_bytes));
        generation.retained_output_bytes = bounded_sum(generation.retained_output_bytes,
            collected.generation.retained_output_bytes, limits.maximum_engine_workspace_bytes);
        if (collected.input.paths.empty()) { continue; }
        auto growth = scoped_workspace_growth{group_charge, allocated_groups.size() + 1U, sizeof(offset_group)};
        if (request.groups.size() == 1U) {
            auto group = offset_group{std::move(collected.input), source.join_type, source.end_type};
            growth.commit();
            finish({&group, 1U});
            return result;
        }
        allocated_groups.emplace_back(std::move(collected.input), source.join_type, source.end_type);
        growth.commit();
    }
    finish(allocated_groups);
    return result;
}

}  // namespace clipper2next::internal
