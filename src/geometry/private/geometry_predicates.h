#pragma once

#include "clipper2next/geometry/line_intersections.h"
#include "clipper2next/geometry/predicates.h"

#include <cstdint>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace clipper2next::internal {

[[nodiscard]] auto products_are_equal(int64_t a, int64_t b, int64_t c, int64_t d) -> bool;

[[nodiscard]] auto cross_product_sign(const Point64& first,
                                      const Point64& second,
                                      const Point64& third) -> int;

// Exact for points whose x/y coordinates satisfy the Clipper coordinate
// contract [MIN_COORD, MAX_COORD]. Call only after request range validation.
[[nodiscard]] inline auto cross_product_sign_in_clipper_range(const Point64& first,
                                         const Point64& second,
                                         const Point64& third) -> int {
    const auto ab_x = second.x - first.x;
    const auto bc_y = third.y - second.y;
    const auto ab_y = second.y - first.y;
    const auto bc_x = third.x - second.x;
#if (defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX
    const auto lhs = static_cast<__int128_t>(ab_x) * static_cast<__int128_t>(bc_y);
    const auto rhs = static_cast<__int128_t>(ab_y) * static_cast<__int128_t>(bc_x);
    return (lhs > rhs) - (lhs < rhs);
#elif defined(_MSC_VER) && defined(_M_X64)
    __int64 lhs_high = 0;
    __int64 rhs_high = 0;
    const auto lhs_low = static_cast<unsigned __int64>(_mul128(ab_x, bc_y, &lhs_high));
    const auto rhs_low = static_cast<unsigned __int64>(_mul128(ab_y, bc_x, &rhs_high));
    if (lhs_high != rhs_high) { return lhs_high > rhs_high ? 1 : -1; }
    return (lhs_low > rhs_low) - (lhs_low < rhs_low);
#else
    return cross_product_sign(first, second, third);
#endif
}

// Fast line interpolation for coordinates already validated against the
// Clipper coordinate contract. The interpolated point remains comfortably
// inside int64_t even after floating-point rounding.
[[nodiscard]] inline auto line_intersection_point_in_clipper_range_fast(
    const Point64& first_start,
    const Point64& first_end,
    const Point64& second_start,
    const Point64& second_end,
    Point64& intersection) -> bool {
    const double first_dx = static_cast<double>(first_end.x - first_start.x);
    const double first_dy = static_cast<double>(first_end.y - first_start.y);
    const double second_dx = static_cast<double>(second_end.x - second_start.x);
    const double second_dy = static_cast<double>(second_end.y - second_start.y);
    const double determinant = first_dy * second_dx - second_dy * first_dx;
    if (determinant == 0.0) { return false; }

    const double ratio =
        (static_cast<double>(first_start.x - second_start.x) * second_dy -
         static_cast<double>(first_start.y - second_start.y) * second_dx) /
        determinant;
    if (ratio <= 0.0) {
        intersection = first_start;
    } else if (ratio >= 1.0) {
        intersection = first_end;
    } else {
        intersection.x = static_cast<int64_t>(first_start.x + ratio * first_dx);
        intersection.y = static_cast<int64_t>(first_start.y + ratio * first_dy);
    }
    return true;
}

[[nodiscard]] auto cross_product_sign(const PointD& first,
                                      const PointD& second,
                                      const PointD& third) -> int;

[[nodiscard]] auto segments_intersect(const Point64& first_start,
                                      const Point64& first_end,
                                      const Point64& second_start,
                                      const Point64& second_end,
                                      bool inclusive) -> bool;

// Strict (non-inclusive) segment intersection for coordinates already
// validated against the Clipper coordinate contract.
[[nodiscard]] auto segments_properly_intersect_in_clipper_range(
    const Point64& first_start,
    const Point64& first_end,
    const Point64& second_start,
    const Point64& second_end) -> bool;

[[nodiscard]] auto point_in_polygon(const Point64& point, const Path64& polygon)
    -> PointInPolygonResult;
[[nodiscard]] auto point_in_polygon(const PointD& point, const PathD& polygon)
    -> PointInPolygonResult;

}  // namespace clipper2next::internal
