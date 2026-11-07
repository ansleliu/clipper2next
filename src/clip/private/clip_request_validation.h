#pragma once

#include "clipper2next/clip/request.h"
#include "clipper2next/geometry/scale.h"

#include <cstdint>

namespace clipper2next::internal {

[[nodiscard]] inline auto validate_clip_parameters(
    ClipType clip_type, FillRule fill_rule, predicate_policy policy) -> clipper_error_code {
    switch (clip_type) {
    case ClipType::NoClip:
    case ClipType::Intersection:
    case ClipType::Union:
    case ClipType::Difference:
    case ClipType::Xor: break;
    default: return clipper_error_code::invalid_argument;
    }
    switch (fill_rule) {
    case FillRule::EvenOdd:
    case FillRule::NonZero:
    case FillRule::Positive:
    case FillRule::Negative: break;
    default: return clipper_error_code::invalid_argument;
    }
    switch (policy.mode) {
    case precision_mode::fast:
    case precision_mode::precise: return clipper_error_code::ok;
    default: return clipper_error_code::invalid_argument;
    }
}

[[nodiscard]] inline auto clip_coordinate_in_range(int64_t value) -> bool {
    return value >= MIN_COORD && value <= MAX_COORD;
}

[[nodiscard]] inline auto clip_path_in_range(const Path64& path) -> bool {
    for (const auto& point : path) {
        if (!clip_coordinate_in_range(point.x) || !clip_coordinate_in_range(point.y)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline auto clip_paths_in_range(const Paths64& paths) -> bool {
    for (const auto& path : paths) {
        if (!clip_path_in_range(path)) { return false; }
    }
    return true;
}

[[nodiscard]] inline auto clip_request_in_range(const clip_request64& request) -> bool {
    return clip_paths_in_range(request.subjects) && clip_paths_in_range(request.open_subjects) &&
           clip_paths_in_range(request.clips);
}

[[nodiscard]] inline auto validate_clip_request(const clip_request64& request)
    -> clipper_error_code {
    if (const auto error = validate_clip_parameters(
            request.clip_type, request.fill_rule, request.options.intersection_policy);
        error != clipper_error_code::ok) {
        return error;
    }
    return clip_request_in_range(request) ? clipper_error_code::ok
                                          : clipper_error_code::coordinate_range;
}

}  // namespace clipper2next::internal
