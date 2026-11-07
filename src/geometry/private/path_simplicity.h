#pragma once

#include "geometry/private/geometry_predicates.h"
#include "geometry/private/geometry_segment_query.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace clipper2next::internal::path_simplicity {

[[nodiscard]] inline auto path_edges_are_adjacent(std::size_t first_index,
                                                  std::size_t second_index,
                                                  std::size_t path_size) noexcept -> bool {
    return second_index == first_index + 1U ||
           (first_index == 0U && second_index + 1U == path_size);
}

// With collinear adjacent edges, the shared vertex must lie between its
// neighbors. Otherwise the edges retrace a nonzero boundary interval.
[[nodiscard]] inline auto collinear_vertex_retraces(const Point64& previous,
                                                   const Point64& current,
                                                   const Point64& next) noexcept -> bool {
    return current.x < (std::min)(previous.x, next.x) ||
           current.x > (std::max)(previous.x, next.x) ||
           current.y < (std::min)(previous.y, next.y) ||
           current.y > (std::max)(previous.y, next.y);
}

// A nonzero result proves a convex-simple boundary and its exact direction.
// Points already satisfy the validated Clipper working-coordinate domain.
// This certificate also rejects multiply-wound polygons (a pentagram
// keeps a consistent turn sign yet self-intersects). With consistent turns
// and no retracing, two x-direction reversals give the two monotone boundary
// chains of one convex cycle. Extra winding adds x reversals. Vertical edges
// do not change x direction; a wholly vertical cycle has no nonzero turn.
template <typename PathLike>
[[nodiscard]] inline auto convex_simple_orientation(const PathLike& path) -> int {
    const auto count = path.size();
    if (count < 3U) { return 0; }

    int turn_sign = 0;
    int first_x_sign = 0;
    int previous_x_sign = 0;
    std::size_t x_flips = 0U;
    for (std::size_t index = 0, previous_index = count - 1U;
         index < count; previous_index = index, ++index) {
        const auto next_index = index + 1U == count ? 0U : index + 1U;
        const auto& previous = path[previous_index];
        const auto& current = path[index];
        const auto& next = path[next_index];
        // Reject duplicate adjacent vertices so "provably simple" callers can
        // pass the ring through without the engine's duplicate cleanup.
        if (current == next) { return 0; }

        const auto turn = clipper2next::internal::cross_product_sign_in_clipper_range(previous, current, next);
        if (turn == 0 && collinear_vertex_retraces(previous, current, next)) { return 0; }
        if (turn != 0) {
            if (turn_sign == 0) {
                turn_sign = turn;
            } else if (turn != turn_sign) {
                return 0;
            }
        }

        const auto x_sign = sign(next.x - current.x);
        if (x_sign != 0) {
            if (previous_x_sign != 0 && x_sign != previous_x_sign) { ++x_flips; }
            if (first_x_sign == 0) { first_x_sign = x_sign; }
            previous_x_sign = x_sign;
        }

    }
    if (previous_x_sign != 0 && first_x_sign != 0 && previous_x_sign != first_x_sign) { ++x_flips; }
    return x_flips <= 2 ? turn_sign : 0;
}

// Points satisfy the same admitted coordinate contract as the convex proof.
// Returns true when the boundary retraces adjacent edges or intersects
// non-adjacent edges. Paths larger than scan_limit remain unproved, so callers
// use the full engine instead of paying the quadratic scan.
template <typename PathLike>
[[nodiscard]] inline auto has_boundary_self_intersection(const PathLike& path,
                                                         std::size_t scan_limit) -> bool {
    const auto count = path.size();
    if (count < 3U) { return true; }
    if (count > scan_limit) { return true; }

    for (std::size_t first = 0, previous_index = count - 1U;
         first < count; previous_index = first, ++first) {
        const auto& first_start = path[first];
        const auto& first_end = path[first + 1U == count ? 0U : first + 1U];
        if (first_start == first_end) { return true; }
        const auto& previous = path[previous_index];
        if (clipper2next::internal::cross_product_sign_in_clipper_range(previous, first_start, first_end) == 0 &&
            collinear_vertex_retraces(previous, first_start, first_end)) {
            return true;
        }

        const geometry_segment_query query{first_start, first_end, false};
        for (std::size_t second = first + 1U; second < count; ++second) {
            if (path_edges_are_adjacent(first, second, count)) { continue; }

            const auto& second_start = path[second];
            const auto& second_end = path[second + 1U == count ? 0U : second + 1U];
            const auto target=geometry_segment_query::segment_bounds(second_start,second_end);
            if (!query.overlaps(target)) {
                continue;
            }
            const auto contacts = query.contacts(second_start, second_end, target);
            if (contacts.proper_crossing || contacts.first_on_target ||
                contacts.second_on_target || contacts.target_first_on_query ||
                contacts.target_second_on_query) { return true; }
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
    if (convex_simple_orientation(path) != 0) { return true; }
    return !has_boundary_self_intersection(path, scan_limit);
}

}  // namespace clipper2next::internal::path_simplicity
