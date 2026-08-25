#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/geometry/math.h"

namespace clipper2next {

CLIPPER2NEXT_API bool products_are_equal(int64_t a, int64_t b, int64_t c, int64_t d);

CLIPPER2NEXT_API int cross_product_sign(
    const Point64& pt1, const Point64& pt2, const Point64& pt3);
CLIPPER2NEXT_API int cross_product_sign(
    const PointD& pt1, const PointD& pt2, const PointD& pt3);

template <typename T>
[[nodiscard]] inline auto is_collinear(const Point<T>& first,
                                       const Point<T>& shared,
                                       const Point<T>& second) -> bool {
    if constexpr (std::is_same_v<T, int64_t>) {
        return cross_product_sign(first, shared, second) == 0;
    } else {
        const auto first_dx =
            coordinate_differences::coordinate_difference_as_double(shared.x, first.x);
        const auto first_dy =
            coordinate_differences::coordinate_difference_as_double(shared.y, first.y);
        const auto second_dx =
            coordinate_differences::coordinate_difference_as_double(second.x, shared.x);
        const auto second_dy =
            coordinate_differences::coordinate_difference_as_double(second.y, shared.y);
        return first_dx * second_dy == first_dy * second_dx;
    }
}

CLIPPER2NEXT_API double cross_product(
    const Point64& pt1, const Point64& pt2, const Point64& pt3);
CLIPPER2NEXT_API double cross_product(
    const PointD& pt1, const PointD& pt2, const PointD& pt3);

CLIPPER2NEXT_API double cross_product(const Point64& vec1, const Point64& vec2);
CLIPPER2NEXT_API double cross_product(const PointD& vec1, const PointD& vec2);

CLIPPER2NEXT_API double perpendicular_distance_from_line_squared(
    const Point64& pt, const Point64& line1, const Point64& line2);
CLIPPER2NEXT_API double perpendicular_distance_from_line_squared(
    const PointD& pt, const PointD& line1, const PointD& line2);

CLIPPER2NEXT_API bool segments_intersect(
    const Point64& seg1a,
    const Point64& seg1b,
    const Point64& seg2a,
    const Point64& seg2b,
    bool inclusive = false);

enum class PointInPolygonResult { IsOn, IsInside, IsOutside };

CLIPPER2NEXT_API PointInPolygonResult point_in_polygon(
    const Point64& pt, const Path64& polygon);
CLIPPER2NEXT_API PointInPolygonResult point_in_polygon(
    const PointD& pt, const PathD& polygon);

}  // namespace clipper2next
