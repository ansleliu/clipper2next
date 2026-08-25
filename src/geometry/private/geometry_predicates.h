#pragma once

#include "clipper2next/geometry/line_intersections.h"
#include "clipper2next/geometry/predicates.h"

namespace clipper2next::internal {

[[nodiscard]] auto products_are_equal(int64_t a, int64_t b, int64_t c, int64_t d) -> bool;

[[nodiscard]] auto cross_product_sign(const Point64& first,
                                      const Point64& second,
                                      const Point64& third) -> int;

// Exact for points whose x/y coordinates satisfy the Clipper coordinate
// contract [MIN_COORD, MAX_COORD]. Call only after request range validation.
[[nodiscard]] auto cross_product_sign_in_clipper_range(const Point64& first,
                                                       const Point64& second,
                                                       const Point64& third) -> int;

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
