#pragma once

#include "clipper2next/clip/request.h"
#include "clipper2next/geometry/scale.h"

#include <cstdint>

namespace clipper2next::internal {

[[nodiscard]] inline auto clip_coordinate_in_range(int64_t value) -> bool {
    return value >= MIN_COORD && value <= MAX_COORD;
}

[[nodiscard]] inline auto clip_rect_in_range(const Rect64& rect) -> bool {
    return clip_coordinate_in_range(rect.left) && clip_coordinate_in_range(rect.top) &&
           clip_coordinate_in_range(rect.right) && clip_coordinate_in_range(rect.bottom);
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

[[nodiscard]] inline auto clip_metadata_in_range(const clip_request_metadata64& metadata) -> bool {
    const bool subject_ok =
        !metadata.single_subject_rect.has_value() ||
        clip_rect_in_range(*metadata.single_subject_rect);
    const bool clip_ok =
        !metadata.single_clip_rect.has_value() || clip_rect_in_range(*metadata.single_clip_rect);
    return subject_ok && clip_ok;
}

}  // namespace clipper2next::internal
