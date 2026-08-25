#include "geometry/private/geometry_predicates.h"

#include <algorithm>

namespace clipper2next {
namespace {

template <typename T>
[[nodiscard]] auto polygon_between_closed(T value, T first, T second) -> bool {
    return value >= (std::min)(first, second) && value <= (std::max)(first, second);
}

template <typename T>
[[nodiscard]] auto point_in_polygon_impl(const Point<T>& point, const Path<T>& polygon)
    -> PointInPolygonResult {
    if (polygon.size() < 3) { return PointInPolygonResult::IsOutside; }

    auto inside = false;
    auto previous = polygon.back();
    for (const auto& current : polygon) {
        // One exact orientation drives both the on-edge test and the rightward-ray
        // crossing test. For Point64 this is an int128 cross-product sign, so the
        // edge requires no long double division, which silently misclassified
        // points once coordinate products exceeded 2^53.
        const auto orientation = cross_product_sign(previous, current, point);
        if (orientation == 0 && polygon_between_closed(point.x, previous.x, current.x) &&
            polygon_between_closed(point.y, previous.y, current.y)) {
            return PointInPolygonResult::IsOn;
        }

        const bool current_above = current.y > point.y;
        if ((previous.y > point.y) != current_above && (orientation > 0) == current_above) {
            inside = !inside;
        }
        previous = current;
    }
    return inside ? PointInPolygonResult::IsInside : PointInPolygonResult::IsOutside;
}

}  // namespace

auto point_in_polygon(const Point64& point, const Path64& polygon) -> PointInPolygonResult {
    return internal::point_in_polygon(point, polygon);
}

auto point_in_polygon(const PointD& point, const PathD& polygon) -> PointInPolygonResult {
    return internal::point_in_polygon(point, polygon);
}

namespace internal {

auto point_in_polygon(const Point64& point, const Path64& polygon) -> PointInPolygonResult {
    return point_in_polygon_impl(point, polygon);
}

auto point_in_polygon(const PointD& point, const PathD& polygon) -> PointInPolygonResult {
    return point_in_polygon_impl(point, polygon);
}

}  // namespace internal
}  // namespace clipper2next
