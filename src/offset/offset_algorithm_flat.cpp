#include "offset/private/offset_algorithm_support.h"

#include "clipper2next/clip/topology.h"
#include "clipper2next/core/path_set_builder.h"
#include "offset/private/offset_group_processor.h"
#include "support/private/scoped_nearest_rounding.h"

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

auto copy_zero_delta_paths(const std::vector<offset_group>& groups, path_set64& solution) -> void {
    solution.reserve(calc_solution_capacity(groups), 0U);
    for (const auto& group : groups) {
        const auto closure = is_closed_path(group.end_type)
                                 ? geotypes::PathClosure::ClosedImplicit
                                 : geotypes::PathClosure::Open;
        for (std::size_t index = 0; index < group.path_count(); ++index) {
            solution.append(group.path(index), closure);
        }
    }
}

auto build_offset_groups(offset_state& state,
                         const std::vector<offset_group>& groups,
                         double delta,
                         const offset_algorithm_options& options,
                         delta_callback_ref delta_callback,
                         const sync_bulk_executor_ref executor,
                         path_set64& solution) -> void {
    state.delta = delta;
    const auto group_options = offset_group_execution_options{
        .miter_limit = options.miter_limit,
        .arc_tolerance = options.arc_tolerance,
        .arc_segments_per_quadrant = options.arc_segments_per_quadrant,
        .reverse_solution = options.reverse_solution,
        .coordinate_rounding = options.coordinate_rounding,
    };
    const auto use_parallel = executor.has_parallel_capability() &&
                              delta_callback == nullptr;
    for (const auto& group : groups) {
        if (use_parallel) {
            build_offset_group_paths_parallel(
                state, group, group_options, state.delta, executor, solution);
        } else {
            build_offset_group_paths(state, group, group_options, delta_callback, solution);
        }
    }
}

class flat_offset_union_writer final {
public:
    explicit flat_offset_union_writer(path_set64& target) noexcept : builder_{target} {}

    [[nodiscard]] auto begin(const topology_layout64& layout) -> clipper_error_code {
        builder_.begin(layout.ring_count, layout.point_count);
        return clipper_error_code::ok;
    }
    [[nodiscard]] auto acquire(const topology_ring_layout64& ring,
                               std::span<geotypes::Point2i64>& destination)
        -> clipper_error_code {
        destination = builder_.acquire(ring.point_count, geotypes::PathClosure::ClosedImplicit);
        return clipper_error_code::ok;
    }
    [[nodiscard]] auto finish() -> clipper_error_code {
        builder_.finish();
        return clipper_error_code::ok;
    }
    auto cancel() noexcept -> void { builder_.cancel(); }

private:
    path_set_builder64 builder_;
};

auto union_offset_solution(path_set64& solution,
                           const offset_algorithm_options& options,
                           bool paths_reversed) -> void {
    path_set64 result;
    flat_offset_union_writer writer{result};
    borrowed_clip_request64 request;
    request.clip_type = ClipType::Union;
    request.fill_rule = paths_reversed ? FillRule::Negative : FillRule::Positive;
    request.subjects = borrow_paths64(solution.view());
    request.options.preserve_collinear = options.preserve_collinear;
    request.options.reverse_solution = options.reverse_solution != paths_reversed;
    request.limits.maximum_input_path_count = solution.size();
    request.limits.maximum_input_point_count = solution.point_count();
    if (!clip_topology_checked(request, make_topology_writer64(writer))) {
        solution.clear();
        return;
    }
    solution = std::move(result);
}

}  // namespace

auto execute_offset_algorithm(offset_state& state,
                              const std::vector<offset_group>& groups,
                              double delta,
                               path_set64& solution,
                               const offset_algorithm_options& options,
                               delta_callback_ref delta_callback,
                               const sync_bulk_executor_ref executor) -> void {
    const auto rounding_guard = scoped_nearest_rounding{};
    solution.clear();
    state.reset();
    if (groups.empty() ||
        (options.check_input_coordinate_range && !offset_groups_in_range(groups)) ||
        !std::isfinite(delta) || !std::isfinite(options.miter_limit) ||
        !std::isfinite(options.arc_tolerance)) {
        return;
    }
    if (std::abs(delta) < 0.5 && delta_callback == nullptr) {
        copy_zero_delta_paths(groups, solution);
        return;
    }

    solution.reserve(calc_solution_capacity(groups), 0U);
    build_offset_groups(
        state, groups, delta, options, delta_callback, executor, solution);
    if (solution.empty() || !offset_solution_in_range(solution)) {
        solution.clear();
        return;
    }

    const auto paths_reversed = check_reverse_orientation(groups);
    if (can_return_direct_convex_offset(groups, delta, nullptr, options, paths_reversed) ||
        can_return_direct_simple_offset(
            groups, solution, delta, nullptr, options, paths_reversed) ||
        can_return_direct_disjoint_simple_offset(
            groups, solution, delta, nullptr, options, paths_reversed)) {
        canonicalize_direct_offset_solution(
            solution, options.reverse_solution != paths_reversed);
        return;
    }
    union_offset_solution(solution, options, paths_reversed);
}

}  // namespace clipper2next::internal
