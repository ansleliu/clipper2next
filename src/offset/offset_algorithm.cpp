#include "offset/private/offset_algorithm.h"
#include "support/private/paths_rebuild.h"
#include "clip/private/clip_execution_strategy.h"
#include "clip/engine/private/polygon_tree_rebuild_access.h"
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

[[nodiscard]] auto calc_solution_capacity(std::span<const offset_group> groups) -> std::size_t {
    std::size_t result = 0;
    for (const auto& group : groups) {
        const auto group_capacity = group.end_type == EndType::Joined
                                        ? checked_size_multiply(group.path_count(), 2U)
                                        : group.path_count();
        result = checked_size_add(result, group_capacity);
    }
    return result;
}

[[nodiscard]] auto check_reverse_orientation(std::span<const offset_group> groups) -> bool {
    bool is_reversed_orientation = false;
    for (const auto& group : groups) {
        if (group.end_type == EndType::Polygon && group.lowest_path_index.has_value()) {
            is_reversed_orientation = group.is_reversed;
            break;
        }
    }
    return is_reversed_orientation;
}

auto normalize_offset_group_output(Paths64& solution, const std::size_t first,
                                   const offset_group& group, const bool reference_reversed) -> void {
    const auto native_reversed = group.end_type == EndType::Polygon && group.is_reversed;
    if (native_reversed == reference_reversed) { return; }
    for (auto index = first; index < solution.size(); ++index) {
        std::ranges::reverse(solution[index]);
    }
}

auto normalize_offset_group_output(path_set64& solution, const std::size_t first,
                                   const offset_group& group, const bool reference_reversed) -> void {
    const auto native_reversed = group.end_type == EndType::Polygon && group.is_reversed;
    if (native_reversed == reference_reversed) { return; }
    for (auto index = first; index < solution.size(); ++index) {
        std::ranges::reverse(solution.mutable_path(index));
    }
}

namespace {

auto copy_zero_delta_paths(std::span<const offset_group> groups, Paths64& solution,
                           bool normalize_winding) -> void {
    Paths64::size_type solution_size = 0;
    for (const auto& group : groups) {
        solution_size = checked_size_add(solution_size, group.path_count());
    }
    solution.reserve(solution_size);
    const auto reference_reversed = check_reverse_orientation(groups);
    for (const auto& group : groups) {
        const auto first = solution.size();
        for (std::size_t path_index = 0; path_index < group.path_count(); ++path_index) {
            const auto path = group.path(path_index);
            auto copy = Path64{};
            copy.assign(path.begin(), path.end());
            solution.emplace_back(std::move(copy));
        }
        if (normalize_winding) { normalize_offset_group_output(solution, first, group, reference_reversed); }
    }
}

auto build_offset_groups(offset_state& state,
                         std::span<const offset_group> groups,
                         double delta,
                         PolyTree64* solution_tree,
                         const offset_algorithm_options& options,
                         delta_callback_ref delta_callback,
                         const sync_bulk_executor_ref executor,
                         Paths64& solution) -> void {
    const auto group_options = offset_group_execution_options{
        .miter_limit = options.miter_limit,
        .arc_tolerance = options.arc_tolerance,
        .arc_segments_per_quadrant = options.arc_segments_per_quadrant,
        .reverse_solution = options.reverse_solution,
        .coordinate_rounding = options.coordinate_rounding,
    };
    const auto can_use_parallel_raw_generation =
        executor.has_parallel_capability() && delta_callback == nullptr && solution_tree == nullptr;
    const auto reference_reversed = check_reverse_orientation(groups);
    for (const auto& group : groups) {
        const auto first = solution.size();
        if (can_use_parallel_raw_generation) {
            build_offset_group_paths_parallel(
                state, group, group_options, delta, executor, solution);
        } else {
            build_offset_group_paths(state, group, group_options, delta, delta_callback, solution);
        }
        normalize_offset_group_output(solution, first, group, reference_reversed);
    }
}

auto union_offset_solution(Paths64& solution,
                           Paths64& destination,
                           PolyTree64* solution_tree,
                           const offset_algorithm_options& options,
                           bool paths_reversed) -> void {
    clip_union_options union_options;
    union_options.fill_rule = paths_reversed ? FillRule::Negative : FillRule::Positive;
    union_options.options.preserve_collinear = options.preserve_collinear;
    union_options.options.reverse_solution = options.reverse_solution != paths_reversed;
    union_options.options.intersection_policy = options.intersection_policy;
    union_options.decompose_disjoint_components = false;

    if (solution_tree) {
        union_closed_paths_into_tree(solution, *solution_tree, union_options);
    } else {
        clip_request64 request;
        request.clip_type = ClipType::Union;
        request.fill_rule = union_options.fill_rule;
        request.subjects = std::move(solution);
        request.options = union_options.options;
        Paths64 open;
        execute_clip_paths_into_validated(request, destination, open);
    }
}

}  // namespace

auto execute_offset_algorithm(offset_state& state,
                              std::span<const offset_group> groups,
                              const double delta,
                              Paths64& destination,
                              PolyTree64* const solution_tree,
                              const offset_algorithm_options& options,
                              const delta_callback_ref delta_callback,
                              const sync_bulk_executor_ref executor) -> void {
    Paths64 solution;  // Raw offset contours, consumed by the authoritative union engine.
    const auto rounding_guard = scoped_nearest_rounding{};
    state.reset();
    if (groups.empty()) {
        destination.clear();
        if (solution_tree) { polygon_tree_rebuild_access::materialize_empty(*solution_tree); }
        return;
    }

    solution.reserve(calc_solution_capacity(groups));

    if (std::abs(delta) < 0.5 && delta_callback == nullptr) {
        copy_zero_delta_paths(groups, solution, solution_tree != nullptr);
        if (solution_tree == nullptr) {
            materialize_paths(std::move(solution), destination);
            return;
        }
    } else {
        build_offset_groups(
            state, groups, delta, solution_tree, options, delta_callback, executor, solution);
    }

    if (solution.empty()) {
        destination.clear();
        if (solution_tree) { polygon_tree_rebuild_access::materialize_empty(*solution_tree); }
        return;
    }
    if (!offset_solution_in_range(solution)) {
        solution.clear();
        raise_clipper_error(clipper_error_code::coordinate_range);
    }

    const auto paths_reversed = check_reverse_orientation(groups);
    if (can_return_direct_simple_offset(
            groups, solution, delta, solution_tree, options, paths_reversed) ||
        can_return_direct_disjoint_simple_offset(
            groups, solution, delta, solution_tree, options, paths_reversed)) {
        canonicalize_direct_offset_solution(solution, options.reverse_solution);
        materialize_paths(std::move(solution), destination);
        return;
    }

    union_offset_solution(solution, destination, solution_tree, options, paths_reversed);
}

}  // namespace clipper2next::internal
