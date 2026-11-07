#include "offset/private/offset_group_processor.h"

#include "clipper2next/geometry/algorithms.h"
#include "offset/private/offset_executor.h"
#include "offset/private/offset_join_processor.h"
#include "offset/private/offset_open_path_processor.h"
#include "offset/private/offset_output.h"
#include "offset/private/offset_single_point.h"
#include "support/private/scoped_nearest_rounding.h"

#include <algorithm>
#include <utility>

namespace clipper2next::internal {
namespace {

auto append_offset_path(offset_state& state, Paths64& output, const Path64& path) -> void {
    auto growth = state.reserve_output(path.size(), sizeof(Path64));
    output.emplace_back(path);
    growth.commit();
}

auto append_offset_path(offset_state& state, path_set64& output, const Path64& path) -> void {
    auto growth = state.reserve_output(path.size(), sizeof(geotypes::PathDescriptor));
    output.append(path, geotypes::PathClosure::ClosedImplicit);
    growth.commit();
}

template <typename Output>
auto offset_polygon(offset_state& state,
                    const offset_group& group,
                    std::span<const Point64> path,
                    const Path64& callback_path,
                    JoinType join_type,
                    const offset_group_execution_options& options,
                    delta_callback_ref delta_callback,
                    Output& output) -> void {
    assert(state.path_out.empty() && state.path_out_charge.bytes() == 0U);
    const auto output_capacity =
        estimate_path_output_capacity(path.size(), join_type, EndType::Polygon, state.arc, state.group_delta);
    state.path_out.reserve(output_capacity);
    const auto point_rounding = delta_callback
        ? offset_point_rounding{options.coordinate_rounding}
        : offset_point_rounding::for_reserved_phase(
              options.coordinate_rounding, state.path_out, output_capacity);

    {
        auto growth = scoped_workspace_growth{delta_callback ? nullptr : &state.path_out_charge};
        // The reserved, callback-free phase only grows constructed points.
        // A tight quota retains the original per-join admission/failure prefix.
        auto* admitted = !delta_callback &&
            growth.try_reserve_slots(output_capacity, sizeof(Point64)) == clipper_error_code::ok
            ? &growth : nullptr;
        const offset_join_options join_options{
            join_type, options.arc_tolerance, options.arc_segments_per_quadrant,
            point_rounding, admitted};
        auto publication = state.publish_growth(delta_callback);
        for (std::size_t current_index = 0, previous_index = path.size() - 1;
             current_index < path.size();
             previous_index = current_index, ++current_index) {
            append_offset_join(state,
                               group,
                               path,
                               callback_path,
                               current_index,
                               previous_index,
                               join_options,
                               delta_callback);
        }
    }
    append_offset_path(state, output, state.path_out);
}

template <typename Output>
auto offset_open_joined(offset_state& state,
                        const offset_group& group,
                        std::span<const Point64> path,
                        const Path64& callback_path,
                        JoinType join_type,
                        const offset_group_execution_options& options,
                        delta_callback_ref delta_callback,
                        Output& output) -> void {
    offset_polygon(
        state, group, path, callback_path, join_type, options,
        delta_callback, output);

    auto reverse_charge = scoped_workspace_charge{state.path_out_charge.context()};
    auto reverse_growth = scoped_workspace_growth{reverse_charge, path.size(), sizeof(Point64)};
    Path64 reverse_path(path.begin(), path.end());
    reverse_growth.commit();
    std::reverse(reverse_path.begin(), reverse_path.end());

    std::reverse(state.normals.begin(), state.normals.end());
    std::rotate(state.normals.begin(), state.normals.begin() + 1, state.normals.end());
    negate_path(state.normals);

    // Joined starts a second output phase after its reverse input is live.
    state.path_out.clear();
    state.path_out_charge.release();
    offset_polygon(state,
                   group,
                   reverse_path,
                   reverse_path,
                   join_type,
                   options,
                   delta_callback,
                   output);
}

template <typename Output>
auto build_offset_path_with_end_type(offset_state& state,
                                     const offset_group& group,
                                     std::span<const Point64> path,
                                     const offset_group_execution_options& options,
                                     delta_callback_ref delta_callback,
                                     JoinType join_type,
                                     EndType end_type,
                                     Output& output) -> void {
    const auto path_length = path.size();
    state.clear_scratch();
    if (delta_callback) {
        auto growth = scoped_workspace_growth{state.callback_charge, path.size(), sizeof(Point64)};
        state.callback_path.assign(path.begin(), path.end());
        growth.commit();
    }

    if (path_length == 0) { return; }
    if (path_length == 1) {
        build_single_point_offset(
            state, group, path.front(), state.callback_path, options, delta_callback);
        if (!state.path_out.empty()) { append_offset_path(state, output, state.path_out); }
        return;
    }

    if ((path_length == 2) && (group.end_type == EndType::Joined)) {
        end_type = group.join_type == JoinType::Round ? EndType::Round : EndType::Square;
    }

    state.construct_normals(path);
    if (end_type == EndType::Polygon) {
        offset_polygon(state,
                       group,
                       path,
                       state.callback_path,
                       join_type,
                       options,
                       delta_callback,
                       output);
    } else if (end_type == EndType::Joined) {
        offset_open_joined(state,
                           group,
                           path,
                           state.callback_path,
                           join_type,
                           options,
                           delta_callback,
                           output);
    } else {
        offset_open_path(state,
                         group,
                         path,
                         state.callback_path,
                         join_type,
                         end_type,
                         options.arc_tolerance,
                         options.arc_segments_per_quadrant,
                         options.coordinate_rounding,
                         delta_callback,
                         output);
    }
}
}  // namespace
auto build_offset_path_result(offset_state& state,
                              const offset_group& group,
                              std::span<const Point64> path,
                              const offset_group_execution_options& options,
                              const double operation_delta,
                              delta_callback_ref delta_callback) -> offset_path_build_result {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options, operation_delta);
    auto result = offset_path_build_result{};
    build_offset_path_with_end_type(
        state, group, path, options, delta_callback,
        group.join_type, group.end_type, result.paths);
    return result;
}
auto append_offset_path(
    offset_state& state,
    const offset_group& group,
    const std::span<const Point64> path,
    const offset_group_execution_options& options,
    const double operation_delta,
    path_set64& output) -> void {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options, operation_delta);
    build_offset_path_with_end_type(
        state, group, path, options, nullptr,
        group.join_type, group.end_type, output);
}
template <typename Output>
auto build_offset_group_paths_impl(offset_state& state,
                                   const offset_group& group,
                                   const offset_group_execution_options& options,
                                   delta_callback_ref delta_callback,
                                   Output& output) -> void {
    for (std::size_t path_index = 0; path_index < group.path_count(); ++path_index) {
        const auto path = group.path(path_index);
        build_offset_path_with_end_type(
            state, group, path, options, delta_callback, group.join_type, group.end_type, output);
    }
}
auto build_offset_group_paths(offset_state& state,
                              const offset_group& group,
                              const offset_group_execution_options& options,
                              const double operation_delta,
                              delta_callback_ref delta_callback,
                              Paths64& output) -> void {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options, operation_delta);
    build_offset_group_paths_impl(state, group, options, delta_callback, output);
}
auto build_offset_group_paths(offset_state& state,
                              const offset_group& group,
                              const offset_group_execution_options& options,
                              const double operation_delta,
                              delta_callback_ref delta_callback,
                              path_set64& output) -> void {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options, operation_delta);
    build_offset_group_paths_impl(state, group, options, delta_callback, output);
}
}  // namespace clipper2next::internal
