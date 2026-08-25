#include "offset/private/offset_open_path_processor.h"

#include "offset/private/offset_executor.h"
#include "offset/private/offset_join_processor.h"
#include "offset/private/offset_output.h"

#include <cmath>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto current_arc_parameters(bool has_delta_callback,
                                          offset_state& state,
                                          double arc_tolerance,
                                          std::size_t arc_segments_per_quadrant)
    -> const offset_arc_parameters& {
    if (has_delta_callback) {
        state.arc = make_arc_parameters(
            state.group_delta, arc_tolerance, arc_segments_per_quadrant);
    }
    return state.arc;
}

auto append_open_start(offset_state& state,
                       std::span<const Point64> path,
                       EndType end_type,
                       double arc_tolerance,
                       std::size_t arc_segments_per_quadrant,
                       geotypes::CoordinateRounding coordinate_rounding,
                       bool has_delta_callback) -> void {
    if (std::fabs(state.group_delta) <= offset_floating_point_tolerance) {
        state.path_out.emplace_back(path[0]);
        return;
    }

    switch (end_type) {
    case EndType::Butt: {
        append_bevel(
            state.path_out, path, state.normals, 0, 0,
            state.group_delta, coordinate_rounding);
        break;
    }
    case EndType::Round: {
        const auto& arc = current_arc_parameters(
            has_delta_callback, state, arc_tolerance,
            arc_segments_per_quadrant);
        append_round(
            state.path_out, path, state.normals, 0, 0,
            state.group_delta, arc, pi, coordinate_rounding);
        break;
    }
    default: {
        append_square(
            state.path_out, path, state.normals, 0, 0,
            state.group_delta, coordinate_rounding);
        break;
    }
    }
}

auto append_open_end(offset_state& state,
                     std::span<const Point64> path,
                     EndType end_type,
                     double arc_tolerance,
                     std::size_t arc_segments_per_quadrant,
                     geotypes::CoordinateRounding coordinate_rounding,
                     bool has_delta_callback,
                     std::size_t high_index) -> void {
    if (std::fabs(state.group_delta) <= offset_floating_point_tolerance) {
        state.path_out.emplace_back(path[high_index]);
        return;
    }

    switch (end_type) {
    case EndType::Butt: {
        append_bevel(
            state.path_out, path, state.normals, high_index, high_index,
            state.group_delta, coordinate_rounding);
        break;
    }
    case EndType::Round: {
        const auto& arc = current_arc_parameters(
            has_delta_callback, state, arc_tolerance,
            arc_segments_per_quadrant);
        append_round(state.path_out,
                     path,
                     state.normals,
                     high_index,
                     high_index,
                     state.group_delta,
                     arc,
                     pi,
                     coordinate_rounding);
        break;
    }
    default: {
        append_square(
            state.path_out, path, state.normals, high_index, high_index,
            state.group_delta, coordinate_rounding);
        break;
    }
    }
}

}  // namespace

template <typename Emit>
auto offset_open_path_impl(offset_state& state,
                           const offset_group& group,
                           std::span<const Point64> path,
                           const Path64& callback_path,
                           JoinType join_type,
                           EndType end_type,
                           double arc_tolerance,
                           std::size_t arc_segments_per_quadrant,
                           geotypes::CoordinateRounding coordinate_rounding,
                           delta_callback_ref delta_callback,
                           Emit&& emit) -> void {
    state.path_out.clear();
    state.path_out.reserve(
        estimate_path_output_capacity(path.size(), join_type, end_type, state.arc));

    if (delta_callback) {
        state.group_delta = delta_callback(callback_path, state.normals, 0, 0);
        if (!std::isfinite(state.group_delta)) { state.group_delta = 0.0; }
    }

    append_open_start(
        state, path, end_type, arc_tolerance, arc_segments_per_quadrant,
        coordinate_rounding,
        static_cast<bool>(delta_callback));

    const auto high_index = path.size() - 1;
    const offset_join_options join_options{
        join_type, arc_tolerance, arc_segments_per_quadrant,
        coordinate_rounding};
    for (Path64::size_type current_index = 1, previous_index = 0; current_index < high_index;
         previous_index = current_index, ++current_index) {
        append_offset_join(
            state,
            group,
            path,
            callback_path,
            current_index,
            previous_index,
            join_options,
            delta_callback);
    }

    for (std::size_t index = high_index; index > 0; --index) {
        state.normals[index] = PointD(-state.normals[index - 1].x, -state.normals[index - 1].y);
    }
    state.normals[0] = state.normals[high_index];

    if (delta_callback) {
        state.group_delta =
            delta_callback(callback_path, state.normals, high_index, high_index);
        if (!std::isfinite(state.group_delta)) { state.group_delta = 0.0; }
    }

    append_open_end(
        state, path, end_type, arc_tolerance, arc_segments_per_quadrant,
        coordinate_rounding,
        static_cast<bool>(delta_callback), high_index);

    for (std::size_t current_index = high_index - 1, previous_index = high_index; current_index > 0;
         previous_index = current_index, --current_index) {
        append_offset_join(
            state,
            group,
            path,
            callback_path,
            current_index,
            previous_index,
            join_options,
            delta_callback);
    }

    emit(state.path_out);
}

auto offset_open_path(offset_state& state,
                      const offset_group& group,
                      std::span<const Point64> path,
                      const Path64& callback_path,
                      JoinType join_type,
                      EndType end_type,
                      double arc_tolerance,
                      std::size_t arc_segments_per_quadrant,
                      geotypes::CoordinateRounding coordinate_rounding,
                      delta_callback_ref delta_callback,
                      Paths64& output) -> void {
    offset_open_path_impl(state,
                          group,
                          path,
                          callback_path,
                           join_type,
                           end_type,
                           arc_tolerance,
                           arc_segments_per_quadrant,
                           coordinate_rounding,
                           delta_callback,
                          [&](const Path64& result) { output.emplace_back(result); });
}

auto offset_open_path(offset_state& state,
                      const offset_group& group,
                      std::span<const Point64> path,
                      const Path64& callback_path,
                      JoinType join_type,
                      EndType end_type,
                      double arc_tolerance,
                      std::size_t arc_segments_per_quadrant,
                      geotypes::CoordinateRounding coordinate_rounding,
                      delta_callback_ref delta_callback,
                      path_set64& output) -> void {
    offset_open_path_impl(state,
                          group,
                          path,
                          callback_path,
                           join_type,
                           end_type,
                           arc_tolerance,
                           arc_segments_per_quadrant,
                           coordinate_rounding,
                           delta_callback,
                          [&](const Path64& result) {
                              output.append(result, geotypes::PathClosure::ClosedImplicit);
                          });
}

}  // namespace clipper2next::internal
