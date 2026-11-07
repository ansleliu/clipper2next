#pragma once

#include "clipper2next/core.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace clipper2next::internal {

[[nodiscard]] inline auto rectclip_coordinate_in_range(std::int64_t value) -> bool {
    constexpr auto span =
        static_cast<std::uint64_t>(MAX_COORD) - static_cast<std::uint64_t>(MIN_COORD);
    return static_cast<std::uint64_t>(value) - static_cast<std::uint64_t>(MIN_COORD) <= span;
}

[[nodiscard]] inline auto rectclip_path_bounds_scalar(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
    if (path.empty()) { return false; }
    auto min_x = path.front().x;
    auto max_x = path.front().x;
    auto min_y = path.front().y;
    auto max_y = path.front().y;
    for (auto point = path.begin() + 1; point != path.end(); ++point) {
        min_x = std::min(min_x, point->x);
        max_x = std::max(max_x, point->x);
        min_y = std::min(min_y, point->y);
        max_y = std::max(max_y, point->y);
    }
    bounds = {min_x, min_y, max_x, max_y};
    return !check_coordinate_range ||
           (rectclip_coordinate_in_range(min_x) && rectclip_coordinate_in_range(min_y) &&
            rectclip_coordinate_in_range(max_x) && rectclip_coordinate_in_range(max_y));
}


[[nodiscard]] auto rectclip_rect_in_range(const Rect64& rect) -> bool;

inline constexpr std::size_t rectclip_vector_bounds_minimum_points = 10U;

[[nodiscard]] auto rectclip_large_path_bounds(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool;

// Keep small paths in the scalar owner; only vector-sized paths cross the
// existing runtime ISA dispatch boundary.
[[nodiscard]] inline auto rectclip_path_bounds(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
    if (path.size() < rectclip_vector_bounds_minimum_points) {
        return rectclip_path_bounds_scalar(path, check_coordinate_range, bounds);
    }
    return rectclip_large_path_bounds(path, check_coordinate_range, bounds);
}

// Prepare one complete ordered bounds vector before any clipping begins.
[[nodiscard]] auto build_rectclip_path_bounds(
    const Paths64& paths, bool check_coordinate_range, std::vector<Rect64>& path_bounds) -> bool;

[[nodiscard]] auto rectclip_rect_strictly_contains_bounds(
    const Rect64& rect, const Rect64& path_bounds) -> bool;

}  // namespace clipper2next::internal
