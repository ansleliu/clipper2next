#pragma once

#include "clipper2next/core.h"

namespace clipper2next::internal {

struct rectclip_unprepared_result final {
    Paths64 paths{};
    bool in_range = true;
};

[[nodiscard]] auto rectclip_unprepared_avx2_supported() noexcept -> bool;
[[nodiscard]] auto rectclip_unprepared_path_bounds_scalar(
    const Path64& path, bool check_coordinate_range, Rect64& path_bounds) -> bool;
[[nodiscard]] auto rectclip_unprepared_path_bounds_avx2(
    const Path64& path, bool check_coordinate_range, Rect64& path_bounds) -> bool;

[[nodiscard]] inline auto rectclip_unprepared_path_bounds(
    const Path64& path,
    bool check_coordinate_range,
    bool use_avx2,
    Rect64& path_bounds) -> bool {
    if (use_avx2 && path.size() >= 8U) {
        return rectclip_unprepared_path_bounds_avx2(
            path, check_coordinate_range, path_bounds);
    }
    return rectclip_unprepared_path_bounds_scalar(
        path, check_coordinate_range, path_bounds);
}

[[nodiscard]] auto execute_rectclip_unprepared_polygons(
    const Rect64& rect,
    const Paths64& paths,
    bool check_coordinate_range)
    -> rectclip_unprepared_result;

}  // namespace clipper2next::internal
