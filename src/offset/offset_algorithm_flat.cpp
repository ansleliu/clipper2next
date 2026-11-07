#include "offset/private/offset_algorithm_support.h"

#include "clipper2next/clip/topology.h"
#include "api/private/borrowed_topology_pipeline.h"
#include "offset/private/offset_group_processor.h"
#include "support/private/scoped_nearest_rounding.h"

#include <cassert>
#include <cmath>
#include <utility>

namespace clipper2next::internal {

auto offset_solution_in_range(const path_set64& solution) noexcept -> bool {
    for (const auto path : solution) {
        for (const auto& point : path) {
            if (point.x < MIN_COORD || point.x > MAX_COORD || point.y < MIN_COORD ||
                point.y > MAX_COORD) {
                return false;
            }
        }
    }
    return true;
}

namespace {

auto copy_zero_delta_paths(std::span<const offset_group> groups, path_set64& solution, scoped_workspace_charge& charge) -> void {
    solution.reserve(calc_solution_capacity(groups), 0U);
    for (const auto& group : groups) {
        const auto closure = is_closed_path(group.end_type) ? geotypes::PathClosure::ClosedImplicit
                                                            : geotypes::PathClosure::Open;
        for (std::size_t index = 0; index < group.path_count(); ++index) {
            auto growth = scoped_workspace_growth{charge, checked_size_add(charge.bytes(), checked_size_add(
                checked_size_multiply(group.path(index).size(), sizeof(Point64)), sizeof(geotypes::PathDescriptor))), 1U};
            solution.append(group.path(index), closure);
            growth.commit();
        }
    }
}

auto build_offset_groups(offset_state& state,
                         std::span<const offset_group> groups,
                         double delta,
                         const offset_algorithm_options& options,
                         delta_callback_ref delta_callback,
                         const sync_bulk_executor_ref executor,
                         path_set64& solution) -> void {
    const auto group_options = offset_group_execution_options{
        .miter_limit = options.miter_limit,
        .arc_tolerance = options.arc_tolerance,
        .arc_segments_per_quadrant = options.arc_segments_per_quadrant,
        .reverse_solution = options.reverse_solution,
        .coordinate_rounding = options.coordinate_rounding,
    };
    const auto use_parallel = executor.has_parallel_capability() && delta_callback == nullptr;
    const auto reference_reversed = check_reverse_orientation(groups);
    for (const auto& group : groups) {
        const auto first = solution.size();
        if (use_parallel) {
            build_offset_group_paths_parallel(
                state, group, group_options, delta, executor, solution);
        } else {
            build_offset_group_paths(state, group, group_options, delta, delta_callback, solution);
        }
        normalize_offset_group_output(solution, first, group, reference_reversed);
    }
}

auto union_offset_solution(path_set64& solution,
                           const offset_algorithm_options& options,
                           bool paths_reversed, scoped_workspace_charge& raw_charge, const offset_engine_resource_context* resources) -> void {
    path_set64 result;
    auto final_charge = scoped_workspace_charge{raw_charge.context(), workspace_category::staging};
    borrowed_clip_request64 request;
    request.clip_type = ClipType::Union;
    request.fill_rule = paths_reversed ? FillRule::Negative : FillRule::Positive;
    request.subjects = borrow_paths64(solution.view());
    request.options.preserve_collinear = options.preserve_collinear;
    request.options.reverse_solution = options.reverse_solution != paths_reversed;
    request.options.intersection_policy = options.intersection_policy;
    request.limits.maximum_input_path_count = solution.size();
    request.limits.maximum_input_point_count = solution.point_count();
    if (resources) {
        request.limits.maximum_output_ring_count = resources->maximum_output_paths;
        request.limits.maximum_output_point_count = resources->maximum_output_points;
    }
    const auto error = execute_borrowed_closed_paths(request, result, final_charge);
    if (error != clipper_error_code::ok) { raise_clipper_error(error); }
    solution = std::move(result);
    raw_charge = std::move(final_charge);
}

}  // namespace

auto execute_offset_algorithm(offset_state& state,
                              std::span<const offset_group> groups,
                              double delta,
                              path_set64& solution,
                              const offset_algorithm_options& options,
                              delta_callback_ref delta_callback,
                              const sync_bulk_executor_ref executor,
                              offset_engine_resource_context* const resources,
                              bool* const output_is_disjoint_simple_shells) -> scoped_workspace_charge {
    if (output_is_disjoint_simple_shells != nullptr) { *output_is_disjoint_simple_shells = false; }
    const auto rounding_guard = scoped_nearest_rounding{};
    solution.clear();
    // The operation owns ready scratch, and the resource scope clears it on exit.
    assert(state.output_charge == nullptr && state.normals.empty() &&
           state.path_out.empty() && state.callback_path.empty());
    auto raw_charge = scoped_workspace_charge{resources};
    auto resource_scope = offset_resource_scope{state, resources, &raw_charge};
    if (groups.empty()) {
        return raw_charge;
    }
    if (std::abs(delta) < 0.5 && delta_callback == nullptr) {
        copy_zero_delta_paths(groups, solution, raw_charge);
        if (const auto error = raw_charge.try_reclassify(workspace_category::staging); error != clipper_error_code::ok) { raise_clipper_error(error); }
        return raw_charge;
    }

    solution.reserve(calc_solution_capacity(groups), 0U);
    build_offset_groups(state, groups, delta, options, delta_callback, executor, solution);
    resource_scope.release();
    if (solution.empty()) { return raw_charge; }
    if (!offset_solution_in_range(solution)) {
        solution.clear();
        raise_clipper_error(clipper_error_code::coordinate_range);
    }

    const auto paths_reversed = check_reverse_orientation(groups);
    const auto direct =
        can_return_direct_simple_offset(
            groups, solution, delta, nullptr, options, paths_reversed) ||
        try_prepare_direct_disjoint_simple_offset(groups, solution, delta, options, paths_reversed, &raw_charge);
    if (resources != nullptr) {
        const auto error = finalize_offset_engine_resources(
            *resources, solution.size(), solution.point_count(), !direct);
        if (error != clipper_error_code::ok) { raise_clipper_error(error); }
    }
    if (direct) {
        if (resources && (solution.size() > resources->maximum_output_paths ||
            solution.point_count() > resources->maximum_output_points)) { raise_clipper_error(clipper_error_code::resource_limit); }
        if (const auto error = raw_charge.try_reclassify(workspace_category::staging); error != clipper_error_code::ok) { raise_clipper_error(error); }
        canonicalize_direct_offset_solution(solution, options.reverse_solution);
        if (output_is_disjoint_simple_shells != nullptr) {
            *output_is_disjoint_simple_shells = true;
        }
        return raw_charge;
    }
    union_offset_solution(solution, options, paths_reversed, raw_charge, resources);
    return raw_charge;
}

}  // namespace clipper2next::internal
