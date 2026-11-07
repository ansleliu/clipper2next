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
    if (has_delta_callback && state.path_out_charge.context()) {
        const auto work = checked_size_multiply(checked_size_add(arc_step_count(state.arc, pi), 3U), 4U);
        const auto error = state.path_out_charge.context()->try_add_work(work);
        if (error != clipper_error_code::ok) { raise_clipper_error(error); }
    }
    return state.arc;
}

auto append_open_start(offset_state& state,
                       std::span<const Point64> path,
                       EndType end_type,
                       double arc_tolerance,
                       std::size_t arc_segments_per_quadrant,
                       const offset_point_rounding& coordinate_rounding,
                       bool has_delta_callback) -> void {
    if (std::fabs(state.group_delta) <= offset_floating_point_tolerance) {
        offset_point_appender{state.path_out, &state.path_out_charge, 1U}.append(path[0]);
        return;
    }

    switch (end_type) {
    case EndType::Butt: {
        append_bevel(
            state.path_out, path, state.normals, 0, 0,
            state.group_delta, coordinate_rounding, &state.path_out_charge);
        break;
    }
    case EndType::Round: {
        const auto& arc = current_arc_parameters(
            has_delta_callback, state, arc_tolerance,
            arc_segments_per_quadrant);
        append_round(
            state.path_out, path, state.normals, 0, 0,
            state.group_delta, arc, pi, coordinate_rounding, &state.path_out_charge);
        break;
    }
    default: {
        append_square(
            state.path_out, path, state.normals, 0, 0,
            state.group_delta, coordinate_rounding, &state.path_out_charge);
        break;
    }
    }
}

auto append_open_end(offset_state& state,
                     std::span<const Point64> path,
                     EndType end_type,
                     double arc_tolerance,
                     std::size_t arc_segments_per_quadrant,
                     const offset_point_rounding& coordinate_rounding,
                     bool has_delta_callback,
                     std::size_t high_index) -> void {
    if (std::fabs(state.group_delta) <= offset_floating_point_tolerance) {
        offset_point_appender{state.path_out, &state.path_out_charge, 1U}.append(path[high_index]);
        return;
    }

    switch (end_type) {
    case EndType::Butt: {
        append_bevel(
            state.path_out, path, state.normals, high_index, high_index,
            state.group_delta, coordinate_rounding, &state.path_out_charge);
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
                     coordinate_rounding, &state.path_out_charge);
        break;
    }
    default: {
        append_square(
            state.path_out, path, state.normals, high_index, high_index,
            state.group_delta, coordinate_rounding, &state.path_out_charge);
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
    assert(state.path_out.empty() && state.path_out_charge.bytes() == 0U);
    const auto output_capacity =
        estimate_path_output_capacity(path.size(), join_type, end_type, state.arc, state.group_delta);
    state.path_out.reserve(output_capacity);

    if (delta_callback) {
        state.group_delta = delta_callback(callback_path, state.normals, 0, 0);
        if (!std::isfinite(state.group_delta)) { raise_clipper_error(clipper_error_code::invalid_argument); }
    }

    const auto point_rounding = delta_callback
        ? offset_point_rounding{coordinate_rounding}
        : offset_point_rounding::for_reserved_phase(
              coordinate_rounding, state.path_out, output_capacity);
    auto publication = state.publish_growth(delta_callback);
    append_open_start(
        state, path, end_type, arc_tolerance, arc_segments_per_quadrant,
        point_rounding,
        static_cast<bool>(delta_callback));

    const auto high_index = path.size() - 1;
    const offset_join_options join_options{
        join_type, arc_tolerance, arc_segments_per_quadrant,
        point_rounding};
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
        if (!std::isfinite(state.group_delta)) { raise_clipper_error(clipper_error_code::invalid_argument); }
    }

    append_open_end(
        state, path, end_type, arc_tolerance, arc_segments_per_quadrant,
        point_rounding,
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

    publication.release();
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
                          [&](const Path64& result) {
                              auto growth = state.reserve_output(result.size(), sizeof(Path64));
                              output.emplace_back(result);
                              growth.commit();
                          });
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
                              auto growth = state.reserve_output(result.size(), sizeof(geotypes::PathDescriptor));
                              output.append(result, geotypes::PathClosure::ClosedImplicit);
                              growth.commit();
                          });
}

}  // namespace clipper2next::internal
