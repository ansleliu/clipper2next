#include "offset/private/offset_single_point.h"
#include "offset/private/offset_output.h"
#include <cmath>

namespace clipper2next::internal {
auto build_single_point_offset(offset_state& state,
                               const offset_group& group,
                               Point64 point,
                               const Path64& callback_path,
                               const offset_group_execution_options& options,
                               delta_callback_ref delta_callback) -> void {
    if (delta_callback) {
        state.group_delta = delta_callback(callback_path, state.normals, 0, 0);
        if (group.is_reversed) { state.group_delta = -state.group_delta; }
    }

    if (!std::isfinite(state.group_delta)) { raise_clipper_error(clipper_error_code::invalid_argument); }
    if (delta_callback) {
        state.arc = make_arc_parameters(
            state.group_delta, options.arc_tolerance, options.arc_segments_per_quadrant);
    }

    const auto abs_delta = std::fabs(state.group_delta);
    if (state.group_delta < 1) { return; }

    auto publication = state.publish_growth(delta_callback);
    if (group.join_type == JoinType::Round) {
        const auto steps = offset_circle_steps(state.arc, state.group_delta);
        if (delta_callback && state.path_out_charge.context()) {
            const auto error = state.path_out_charge.context()->try_add_work(checked_size_multiply(steps, 4U));
            if (error != clipper_error_code::ok) { raise_clipper_error(error); }
        }
        auto append = offset_point_appender{state.path_out, &state.path_out_charge, steps};
        state.path_out.reserve(steps);
        const auto sine = std::sin(2 * pi / static_cast<double>(steps));
        const auto cosine = std::cos(2 * pi / static_cast<double>(steps));
        const auto point_rounding = offset_point_rounding::for_reserved_phase(
            options.coordinate_rounding, state.path_out, steps);
        auto vector = PointD{1, 0};
        for (std::size_t index = 0; index < steps; ++index) {
            // Exact cardinal vectors avoid trigonometric residuals at half-grid ties.
            auto direction = vector;
            if (index == 0U) { direction = {1, 0}; }
            else if (steps % 2U == 0U && index == steps / 2U) { direction = {-1, 0}; }
            else if (steps % 4U == 0U && index == steps / 4U) { direction = {0, 1}; }
            else if (steps % 4U == 0U && index == steps - steps / 4U) { direction = {0, -1}; }
            append.append(offset_point(
                {point.x + abs_delta * direction.x, point.y + abs_delta * direction.y},
                point_rounding));
            vector = {vector.x * cosine - vector.y * sine,
                      vector.y * cosine + vector.x * sine};
        }
        // A finite radius >= 1 must generate a circle; empty signals an
        // unrepresentable ellipse, not a successful empty offset.
        if (state.path_out.empty()) { raise_clipper_error(clipper_error_code::coordinate_range); }
    } else {
        const auto delta = std::ceil(abs_delta);
        auto append = offset_point_appender{state.path_out, &state.path_out_charge, 4U};
        state.path_out.reserve(4U);
        const auto point_rounding = offset_point_rounding::for_reserved_phase(
            options.coordinate_rounding, state.path_out, 4U);
        append.append(offset_point({point.x - delta, point.y - delta}, point_rounding));
        append.append(offset_point({point.x + delta, point.y - delta}, point_rounding));
        append.append(offset_point({point.x + delta, point.y + delta}, point_rounding));
        append.append(offset_point({point.x - delta, point.y + delta}, point_rounding));
    }
}
}  // namespace clipper2next::internal
