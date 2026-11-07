#pragma once

#include "clipper2next/offset/operations.h"

#include <cmath>

namespace clipper2next::internal {

[[nodiscard]] inline auto validate_offset_parameters(
    double delta, double miter_limit, double arc_tolerance,
    geotypes::CoordinateRounding rounding, predicate_policy policy) -> clipper_error_code {
    if (!std::isfinite(delta) || !std::isfinite(miter_limit) || !std::isfinite(arc_tolerance) ||
        (rounding != geotypes::CoordinateRounding::NearestEven &&
         rounding != geotypes::CoordinateRounding::NearestAwayFromZero) ||
        (policy.mode != precision_mode::fast && policy.mode != precision_mode::precise)) {
        return clipper_error_code::invalid_argument;
    }
    return clipper_error_code::ok;
}

[[nodiscard]] inline auto validate_offset_group_types(JoinType join, EndType end)
    -> clipper_error_code {
    const auto valid_join = join == JoinType::Square || join == JoinType::Bevel ||
                            join == JoinType::Round || join == JoinType::Miter;
    const auto valid_end = end == EndType::Polygon || end == EndType::Joined ||
                           end == EndType::Butt || end == EndType::Square || end == EndType::Round;
    return valid_join && valid_end ? clipper_error_code::ok : clipper_error_code::invalid_argument;
}

[[nodiscard]] inline auto validate_offset_request(const offset_request64& request)
    -> clipper_error_code {
    const auto error = validate_offset_parameters(request.delta, request.miter_limit,
        request.arc_tolerance, request.coordinate_rounding, request.options.intersection_policy);
    return error == clipper_error_code::ok
        ? validate_offset_group_types(request.join_type, request.end_type) : error;
}

[[nodiscard]] inline auto validate_offset_request(const borrowed_offset_request64& request)
    -> clipper_error_code {
    const auto error = validate_offset_parameters(request.delta, request.miter_limit,
        request.arc_tolerance, request.coordinate_rounding, request.options.intersection_policy);
    if (error != clipper_error_code::ok) { return error; }
    for (const auto& group : request.groups) {
        const auto group_error = validate_offset_group_types(group.join_type, group.end_type);
        if (group_error != clipper_error_code::ok) { return group_error; }
    }
    return clipper_error_code::ok;
}

}  // namespace clipper2next::internal
