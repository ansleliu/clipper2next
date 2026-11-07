#include "offset/private/offset_join_processor.h"

#include "offset/private/offset_output.h"

#include <algorithm>
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

}  // namespace

auto append_offset_join(offset_state& state,
                        const offset_group& group,
                        std::span<const Point64> path,
                        const Path64& callback_path,
                        std::size_t current_index,
                        std::size_t previous_index,
                        const offset_join_options& options,
                        delta_callback_ref delta_callback) -> void {
    if (path[current_index] == path[previous_index]) { return; }

    auto sin_a = cross_product(state.normals[current_index], state.normals[previous_index]);
    const auto cos_a = dot_product(state.normals[current_index], state.normals[previous_index]);
    sin_a = std::clamp(sin_a, -1.0, 1.0);

    if (delta_callback) {
        state.group_delta =
            delta_callback(callback_path, state.normals, current_index, previous_index);
        if (group.is_reversed) { state.group_delta = -state.group_delta; }
    }

    if (!std::isfinite(state.group_delta)) {
        raise_clipper_error(clipper_error_code::invalid_argument);
    }

    if (std::fabs(state.group_delta) <= offset_floating_point_tolerance) {
        offset_point_appender{state.path_out, &state.path_out_charge, 1U, options.output_growth}.append(path[current_index]);
        return;
    }

    if (cos_a > -0.999 && (sin_a * state.group_delta < 0)) {
        append_perpendicular(
            state.path_out, path[current_index], state.normals[previous_index],
            state.group_delta, options.coordinate_rounding, &state.path_out_charge, options.output_growth);
        offset_point_appender{state.path_out, &state.path_out_charge, 1U, options.output_growth}.append(path[current_index]);
        append_perpendicular(
            state.path_out, path[current_index], state.normals[current_index],
            state.group_delta, options.coordinate_rounding, &state.path_out_charge, options.output_growth);
    } else if (cos_a > 0.999 && options.join_type != JoinType::Round) {
        append_miter(state.path_out,
                     path,
                     state.normals,
                     current_index,
                     previous_index,
                     state.group_delta,
                     cos_a,
                     options.coordinate_rounding, &state.path_out_charge, options.output_growth);
    } else if (options.join_type == JoinType::Miter) {
        if (cos_a > state.temp_limit - 1) {
            append_miter(state.path_out,
                         path,
                         state.normals,
                         current_index,
                         previous_index,
                         state.group_delta,
                         cos_a,
                         options.coordinate_rounding, &state.path_out_charge, options.output_growth);
        } else {
            append_square(state.path_out,
                          path,
                          state.normals,
                          current_index,
                          previous_index,
                          state.group_delta,
                          options.coordinate_rounding, &state.path_out_charge, options.output_growth);
        }
    } else if (options.join_type == JoinType::Round) {
        const auto& arc = current_arc_parameters(
            static_cast<bool>(delta_callback), state, options.arc_tolerance,
            options.arc_segments_per_quadrant);
        append_round(state.path_out,
                     path,
                     state.normals,
                     current_index,
                     previous_index,
                     state.group_delta,
                     arc,
                     std::atan2(sin_a, cos_a),
                     options.coordinate_rounding, &state.path_out_charge, options.output_growth);
    } else if (options.join_type == JoinType::Bevel) {
        append_bevel(
            state.path_out, path, state.normals, current_index, previous_index,
            state.group_delta, options.coordinate_rounding, &state.path_out_charge, options.output_growth);
    } else {
        append_square(
            state.path_out, path, state.normals, current_index, previous_index,
            state.group_delta, options.coordinate_rounding, &state.path_out_charge, options.output_growth);
    }
}

}  // namespace clipper2next::internal
