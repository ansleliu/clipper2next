#pragma once

#include "clipper2next/geometry/predicates.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace clipper2next::internal::path_simplicity {

[[nodiscard]] inline auto values_overlap_closed(int64_t first_min,
                                                int64_t first_max,
                                                int64_t second_min,
                                                int64_t second_max) noexcept -> bool {
    return (std::max)(first_min, second_min) <= (std::min)(first_max, second_max);
}

[[nodiscard]] inline auto segment_bounds_overlap(const Point64& first_start,
                                                 const Point64& first_end,
                                                 const Point64& second_start,
                                                 const Point64& second_end) noexcept -> bool {
    return values_overlap_closed((std::min)(first_start.x, first_end.x),
                                 (std::max)(first_start.x, first_end.x),
                                 (std::min)(second_start.x, second_end.x),
                                 (std::max)(second_start.x, second_end.x)) &&
           values_overlap_closed((std::min)(first_start.y, first_end.y),
                                 (std::max)(first_start.y, first_end.y),
                                 (std::min)(second_start.y, second_end.y),
                                 (std::max)(second_start.y, second_end.y));
}

[[nodiscard]] inline auto collinear_segments_overlap(const Point64& first_start,
                                                     const Point64& first_end,
                                                     const Point64& second_start,
                                                     const Point64& second_end) -> bool {
    return is_collinear(first_start, first_end, second_start) &&
           is_collinear(first_start, first_end, second_end) &&
           segment_bounds_overlap(first_start, first_end, second_start, second_end);
}

[[nodiscard]] inline auto path_edges_are_adjacent(std::size_t first_index,
                                                  std::size_t second_index,
                                                  std::size_t path_size) noexcept -> bool {
    return second_index == first_index + 1U ||
           (first_index == 0U && second_index + 1U == path_size);
}

// Exact convexity test that also rejects multiply-wound polygons (a pentagram
// keeps a consistent turn sign yet self-intersects). Convex simple polygons
// reverse their x and y edge directions exactly twice around the cycle; any
// additional reversal implies extra turning, so the path cannot be convex.
template <typename PathLike>
[[nodiscard]] inline auto is_convex_simple_polygon(const PathLike& path) -> bool {
    const auto count = path.size();
    if (count < 3U) { return false; }

    int turn_sign = 0;
    int first_x_sign = 0;
    int previous_x_sign = 0;
    int x_flips = 0;
    int first_y_sign = 0;
    int previous_y_sign = 0;
    int y_flips = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& previous = path[(index + count - 1U) % count];
        const auto& current = path[index];
        const auto& next = path[(index + 1U) % count];
        // Reject duplicate adjacent vertices so "provably simple" callers can
        // pass the ring through without the engine's duplicate cleanup.
        if (current == next) { return false; }

        const auto turn = cross_product_sign(previous, current, next);
        if (turn != 0) {
            if (turn_sign == 0) {
                turn_sign = turn;
            } else if (turn != turn_sign) {
                return false;
            }
        }

        const auto x_sign = sign(next.x - current.x);
        if (x_sign != 0) {
            if (previous_x_sign != 0 && x_sign != previous_x_sign) { ++x_flips; }
            if (first_x_sign == 0) { first_x_sign = x_sign; }
            previous_x_sign = x_sign;
        }
        const auto y_sign = sign(next.y - current.y);
        if (y_sign != 0) {
            if (previous_y_sign != 0 && y_sign != previous_y_sign) { ++y_flips; }
            if (first_y_sign == 0) { first_y_sign = y_sign; }
            previous_y_sign = y_sign;
        }
    }
    if (previous_x_sign != 0 && first_x_sign != 0 && previous_x_sign != first_x_sign) { ++x_flips; }
    if (previous_y_sign != 0 && first_y_sign != 0 && previous_y_sign != first_y_sign) { ++y_flips; }
    return turn_sign != 0 && x_flips <= 2 && y_flips <= 2;
}

// Returns true when the path has (or may have) a self-intersection between
// non-adjacent edges. Paths larger than scan_limit are reported as
// intersecting so callers fall back to the full engine instead of paying the
// quadratic scan.
template <typename PathLike>
[[nodiscard]] inline auto has_non_adjacent_self_intersection(const PathLike& path,
                                                             std::size_t scan_limit) -> bool {
    if (path.size() < 4U) { return false; }
    if (path.size() > scan_limit) { return true; }

    for (std::size_t first = 0; first < path.size(); ++first) {
        const auto& first_start = path[first];
        const auto& first_end = path[(first + 1U) % path.size()];
        if (first_start == first_end) { return true; }

        for (std::size_t second = first + 1U; second < path.size(); ++second) {
            if (path_edges_are_adjacent(first, second, path.size())) { continue; }

            const auto& second_start = path[second];
            const auto& second_end = path[(second + 1U) % path.size()];
            if (second_start == second_end) { return true; }
            if (!segment_bounds_overlap(first_start, first_end, second_start, second_end)) {
                continue;
            }
            if (segments_intersect(first_start, first_end, second_start, second_end, true) ||
                collinear_segments_overlap(first_start, first_end, second_start, second_end)) {
                return true;
            }
        }
    }
    return false;
}

// Cheap exact convexity first (O(n)), quadratic edge scan only for small
// non-convex paths; anything larger is treated as "not provably simple".
template <typename PathLike>
[[nodiscard]] inline auto path_is_provably_simple(const PathLike& path, std::size_t scan_limit)
    -> bool {
    if (path.size() < 3U) { return false; }
    if (is_convex_simple_polygon(path)) { return true; }
    return !has_non_adjacent_self_intersection(path, scan_limit);
}

}  // namespace clipper2next::internal::path_simplicity
