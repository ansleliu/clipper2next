#include "clipper2next/geometry/predicates.h"

#include "geometry/private/geometry_predicates.h"

namespace clipper2next {

auto products_are_equal(int64_t a, int64_t b, int64_t c, int64_t d) -> bool {
    return internal::products_are_equal(a, b, c, d);
}

auto cross_product_sign(const Point64& first, const Point64& second, const Point64& third) -> int {
    return internal::cross_product_sign(first, second, third);
}

auto cross_product_sign(const PointD& first, const PointD& second, const PointD& third) -> int {
    return internal::cross_product_sign(first, second, third);
}

auto cross_product(const Point64& first, const Point64& second, const Point64& third) -> double {
    const auto ab_x = coordinate_differences::coordinate_difference_as_double(second.x, first.x);
    const auto ab_y = coordinate_differences::coordinate_difference_as_double(second.y, first.y);
    const auto bc_x = coordinate_differences::coordinate_difference_as_double(third.x, second.x);
    const auto bc_y = coordinate_differences::coordinate_difference_as_double(third.y, second.y);
    return ab_x * bc_y - ab_y * bc_x;
}

auto cross_product(const PointD& first, const PointD& second, const PointD& third) -> double {
    const auto ab_x = second.x - first.x;
    const auto ab_y = second.y - first.y;
    const auto bc_x = third.x - second.x;
    const auto bc_y = third.y - second.y;
    return ab_x * bc_y - ab_y * bc_x;
}

auto cross_product(const Point64& first, const Point64& second) -> double {
    return static_cast<double>(first.y) * static_cast<double>(second.x) -
           static_cast<double>(second.y) * static_cast<double>(first.x);
}

auto cross_product(const PointD& first, const PointD& second) -> double {
    return first.y * second.x - second.y * first.x;
}

auto perpendicular_distance_from_line_squared(const Point64& point,
                                               const Point64& line_start,
                                               const Point64& line_end) -> double {
    const auto line_dx =
        coordinate_differences::coordinate_difference_as_double(line_end.x, line_start.x);
    const auto line_dy =
        coordinate_differences::coordinate_difference_as_double(line_end.y, line_start.y);
    if (line_dx == 0.0 && line_dy == 0.0) { return 0.0; }
    const auto point_dx =
        coordinate_differences::coordinate_difference_as_double(point.x, line_start.x);
    const auto point_dy =
        coordinate_differences::coordinate_difference_as_double(point.y, line_start.y);
    return square(point_dx * line_dy - line_dx * point_dy) /
           (line_dx * line_dx + line_dy * line_dy);
}

auto perpendicular_distance_from_line_squared(const PointD& point,
                                               const PointD& line_start,
                                               const PointD& line_end) -> double {
    const auto line_dx = line_end.x - line_start.x;
    const auto line_dy = line_end.y - line_start.y;
    if (line_dx == 0.0 && line_dy == 0.0) { return 0.0; }
    const auto point_dx = point.x - line_start.x;
    const auto point_dy = point.y - line_start.y;
    return square(point_dx * line_dy - line_dx * point_dy) /
           (line_dx * line_dx + line_dy * line_dy);
}

auto segments_intersect(const Point64& first_start,
                        const Point64& first_end,
                        const Point64& second_start,
                        const Point64& second_end,
                        bool inclusive) -> bool {
    return internal::segments_intersect(
        first_start, first_end, second_start, second_end, inclusive);
}

}  // namespace clipper2next
