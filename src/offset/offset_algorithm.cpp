#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_algorithm_support.h"

#include "offset/private/offset_group_processor.h"
#include "clip/private/boolean_union_service.h"
#include "clipper2next/geometry/scale.h"
#include "clipper2next/clip/topology.h"
#include "clipper2next/core/path_set_builder.h"
#include "support/private/checked_size.h"
#include "support/private/scoped_nearest_rounding.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace clipper2next::internal {

[[nodiscard]] auto offset_groups_in_range(const std::vector<offset_group>& groups) noexcept
    -> bool {
    for (const auto& group : groups) {
        for (std::size_t path_index = 0; path_index < group.path_count(); ++path_index) {
            const auto path = group.path(path_index);
            for (const auto& point : path) {
                if (point.x < MIN_COORD || point.x > MAX_COORD || point.y < MIN_COORD ||
                    point.y > MAX_COORD) {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] auto offset_solution_in_range(const Paths64& solution) noexcept -> bool {
    for (const auto& path : solution) {
        for (const auto& point : path) {
            if (point.x < MIN_COORD || point.x > MAX_COORD || point.y < MIN_COORD ||
                point.y > MAX_COORD) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto calc_solution_capacity(const std::vector<offset_group>& groups) -> std::size_t {
    std::size_t result = 0;
    for (const auto& group : groups) {
        const auto group_capacity = group.end_type == EndType::Joined
                                        ? checked_size_multiply(group.path_count(), 2U)
                                        : group.path_count();
        result = checked_size_add(result, group_capacity);
    }
    return result;
}

[[nodiscard]] auto check_reverse_orientation(const std::vector<offset_group>& groups) -> bool {
    bool is_reversed_orientation = false;
    for (const auto& group : groups) {
        if (group.end_type == EndType::Polygon) {
            is_reversed_orientation = group.is_reversed;
            break;
        }
    }
    return is_reversed_orientation;
}

namespace {

auto copy_zero_delta_paths(const std::vector<offset_group>& groups, Paths64& solution) -> void {
    Paths64::size_type solution_size = 0;
    for (const auto& group : groups) {
        solution_size = checked_size_add(solution_size, group.path_count());
    }
    solution.reserve(solution_size);
    for (const auto& group : groups) {
        for (std::size_t path_index = 0; path_index < group.path_count(); ++path_index) {
            const auto path = group.path(path_index);
            auto copy = Path64{};
            copy.assign(path.begin(), path.end());
            solution.emplace_back(std::move(copy));
        }
    }
}

auto build_offset_groups(offset_state& state,
                         const std::vector<offset_group>& groups,
                         double delta,
                         PolyTree64* solution_tree,
                         const offset_algorithm_options& options,
                         delta_callback_ref delta_callback,
                         const sync_bulk_executor_ref executor,
                         Paths64& solution) -> void {
    state.delta = delta;
    const auto group_options = offset_group_execution_options{
        .miter_limit = options.miter_limit,
        .arc_tolerance = options.arc_tolerance,
        .arc_segments_per_quadrant = options.arc_segments_per_quadrant,
        .reverse_solution = options.reverse_solution,
        .coordinate_rounding = options.coordinate_rounding,
    };
    const auto can_use_parallel_raw_generation =
        executor.has_parallel_capability() && delta_callback == nullptr && solution_tree == nullptr;
    for (const auto& group : groups) {
        if (can_use_parallel_raw_generation) {
            build_offset_group_paths_parallel(
                state, group, group_options, state.delta, executor, solution);
        } else {
            build_offset_group_paths(state, group, group_options, delta_callback, solution);
        }
    }
}

auto union_offset_solution(Paths64& solution,
                           PolyTree64* solution_tree,
                           const offset_algorithm_options& options,
                           bool paths_reversed) -> void {
    clip_union_options union_options;
    union_options.fill_rule = paths_reversed ? FillRule::Negative : FillRule::Positive;
    union_options.options.preserve_collinear = options.preserve_collinear;
    union_options.options.reverse_solution = options.reverse_solution != paths_reversed;
    union_options.decompose_disjoint_components = false;

    if (solution_tree) {
        union_closed_paths_into_tree(solution, *solution_tree, union_options);
    } else {
        solution = union_closed_paths(std::move(solution), union_options);
    }
}

}  // namespace

void execute_offset_algorithm_impl(offset_state& state,
                                   const std::vector<offset_group>& groups,
                                   const double delta,
                                   Paths64& solution,
                                   PolyTree64* const solution_tree,
                                   const offset_algorithm_options& options,
                                   const delta_callback_ref delta_callback,
                                   const sync_bulk_executor_ref executor) {
    const auto rounding_guard = scoped_nearest_rounding{};
    state.reset();
    if (groups.empty() ||
        (options.check_input_coordinate_range && !offset_groups_in_range(groups)) ||
        !std::isfinite(delta) || !std::isfinite(options.miter_limit) ||
        !std::isfinite(options.arc_tolerance)) {
        return;
    }

    solution.reserve(calc_solution_capacity(groups));

    if (std::abs(delta) < 0.5 && delta_callback == nullptr) {
        copy_zero_delta_paths(groups, solution);
        if (solution_tree == nullptr) { return; }
    } else {
        build_offset_groups(
            state, groups, delta, solution_tree, options, delta_callback, executor, solution);
    }

    if (solution.empty()) { return; }
    if (!offset_solution_in_range(solution)) {
        solution.clear();
        return;
    }

    const auto paths_reversed = check_reverse_orientation(groups);
    if (can_return_direct_simple_offset(
            groups, solution, delta, solution_tree, options, paths_reversed) ||
        can_return_direct_disjoint_simple_offset(
            groups, solution, delta, solution_tree, options, paths_reversed)) {
        canonicalize_direct_offset_solution(solution, options.reverse_solution != paths_reversed);
        return;
    }

    union_offset_solution(solution, solution_tree, options, paths_reversed);
}

auto execute_offset_algorithm_scalar_reference(offset_state& state,
                                               const std::vector<offset_group>& groups,
                                               const double delta,
                                               Paths64& solution,
                                               PolyTree64* const solution_tree,
                                               const offset_algorithm_options& options,
                                               const delta_callback_ref delta_callback) -> void {
    execute_offset_algorithm_impl(
        state, groups, delta, solution, solution_tree, options, delta_callback, {});
}

auto execute_offset_algorithm(offset_state& state,
                              const std::vector<offset_group>& groups,
                              double delta,
                              Paths64& solution,
                              PolyTree64* solution_tree,
                              const offset_algorithm_options& options,
                              delta_callback_ref delta_callback,
                              const sync_bulk_executor_ref executor) -> void {
    execute_offset_algorithm_impl(
        state, groups, delta, solution, solution_tree, options, delta_callback, executor);
}

}  // namespace clipper2next::internal
