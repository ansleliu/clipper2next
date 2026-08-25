#include "offset/private/offset_group_processor.h"

#include "clipper2next/geometry/algorithms.h"
#include "offset/private/offset_executor.h"
#include "offset/private/offset_join_processor.h"
#include "offset/private/offset_open_path_processor.h"
#include "offset/private/offset_output.h"
#include "support/private/scoped_nearest_rounding.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace clipper2next::internal {
namespace {

auto append_offset_path(Paths64& output, const Path64& path) -> void {
    output.emplace_back(path);
}

auto append_offset_path(path_set64& output, const Path64& path) -> void {
    output.append(path, geotypes::PathClosure::ClosedImplicit);
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
    state.path_out.clear();
    state.path_out.reserve(
        estimate_path_output_capacity(path.size(), join_type, EndType::Polygon, state.arc));

    const offset_join_options join_options{
        join_type, options.arc_tolerance, options.arc_segments_per_quadrant,
        options.coordinate_rounding};
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

    append_offset_path(output, state.path_out);
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

    Path64 reverse_path(path.begin(), path.end());
    std::reverse(reverse_path.begin(), reverse_path.end());

    std::reverse(state.normals.begin(), state.normals.end());
    state.normals.emplace_back(state.normals[0]);
    state.normals.erase(state.normals.begin());
    negate_path(state.normals);

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
auto append_single_point_offset(offset_state& state,
                                const offset_group& group,
                                std::span<const Point64> path,
                                const Path64& callback_path,
                                const offset_group_execution_options& options,
                                delta_callback_ref delta_callback,
                                Output& output) -> void {
    if (delta_callback) {
        state.group_delta = delta_callback(callback_path, state.normals, 0, 0);
        if (group.is_reversed) { state.group_delta = -state.group_delta; }
    }

    if (!std::isfinite(state.group_delta)) { return; }

    const auto abs_delta = std::fabs(state.group_delta);
    if (state.group_delta < 1) { return; }

    state.path_out.reserve(estimate_single_point_output_capacity(group.join_type, state.arc));
    const auto& point = path[0];
    if (group.join_type == JoinType::Round) {
        const auto steps = arc_step_count(state.arc, 2 * pi);
        state.path_out = make_ellipse(point, abs_delta, abs_delta, steps);
    } else {
        const auto delta = std::ceil(abs_delta);
        const auto makePoint = [rounding = options.coordinate_rounding](
                                   double x, double y) {
            return Point64{
                geotypes::coordinateCast<std::int64_t>(x, rounding),
                geotypes::coordinateCast<std::int64_t>(y, rounding)};
        };
        state.path_out = Path64{
            makePoint(point.x - delta, point.y - delta),
            makePoint(point.x + delta, point.y - delta),
            makePoint(point.x + delta, point.y + delta),
            makePoint(point.x - delta, point.y + delta)};
    }
    append_offset_path(output, state.path_out);
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
    state.path_out.clear();
    state.callback_path.clear();
    if (delta_callback) { state.callback_path.assign(path.begin(), path.end()); }

    if (path_length == 1) {
        append_single_point_offset(
            state, group, path, state.callback_path, options, delta_callback, output);
        return;
    }

    if ((path_length == 2) && (group.end_type == EndType::Joined)) {
        end_type = group.join_type == JoinType::Round ? EndType::Round : EndType::Square;
    }

    assign_normals(state.normals, path);
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
                              delta_callback_ref delta_callback) -> offset_path_build_result {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options);
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
    path_set64& output) -> void {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options);
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
    auto join_type = group.join_type;
    auto end_type = group.end_type;
    for (std::size_t path_index = 0; path_index < group.path_count(); ++path_index) {
        const auto path = group.path(path_index);
        build_offset_path_with_end_type(
            state, group, path, options, delta_callback, join_type, end_type, output);
        if ((path.size() == 2) && (group.end_type == EndType::Joined)) {
            end_type = group.join_type == JoinType::Round ? EndType::Round : EndType::Square;
        }
    }
}
auto build_offset_group_paths(offset_state& state,
                              const offset_group& group,
                              const offset_group_execution_options& options,
                              delta_callback_ref delta_callback,
                              Paths64& output) -> void {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options);
    build_offset_group_paths_impl(state, group, options, delta_callback, output);
}
auto build_offset_group_paths(offset_state& state,
                              const offset_group& group,
                              const offset_group_execution_options& options,
                              delta_callback_ref delta_callback,
                              path_set64& output) -> void {
    const auto rounding_guard = scoped_nearest_rounding{};
    prepare_offset_group_state(state, group, options);
    build_offset_group_paths_impl(state, group, options, delta_callback, output);
}
}  // namespace clipper2next::internal
