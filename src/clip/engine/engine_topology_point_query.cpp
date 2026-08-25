#include "clip/engine/private/engine_topology.h"

#include "clipper2next/geometry/algorithms.h"
#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {

auto point_in_output_polygon(const Point64& point, output_point_node* output_point)
    -> PointInPolygonResult {
    if (output_point == output_point->next || output_point->prev == output_point->next) {
        return PointInPolygonResult::IsOutside;
    }

    auto* start = output_point;
    do {
        if (output_point->pt.y != point.y) { break; }
        output_point = output_point->next.get();
    } while (output_point != start);
    if (output_point->pt.y == point.y) { return PointInPolygonResult::IsOutside; }

    bool is_above = output_point->pt.y < point.y;
    const bool starting_above = is_above;
    auto value = 0;
    auto* current = output_point->next.get();
    while (current != output_point) {
        if (is_above) {
            while (current != output_point && current->pt.y < point.y) {
                current = current->next.get();
            }
        } else {
            while (current != output_point && current->pt.y > point.y) {
                current = current->next.get();
            }
        }
        if (current == output_point) { break; }

        if (current->pt.y == point.y) {
            if (current->pt.x == point.x ||
                (current->pt.y == current->prev->pt.y &&
                 (point.x < current->prev->pt.x) != (point.x < current->pt.x))) {
                return PointInPolygonResult::IsOn;
            }

            current = current->next.get();
            if (current == output_point) { break; }
            continue;
        }

        if (point.x < current->pt.x && point.x < current->prev->pt.x) {
        } else if (point.x > current->prev->pt.x && point.x > current->pt.x) {
            value = 1 - value;
        } else {
            const auto sign = cross_product_sign_in_clipper_range(
                current->prev->pt, current->pt, point);
            if (sign == 0) { return PointInPolygonResult::IsOn; }
            if ((sign < 0) == is_above) { value = 1 - value; }
        }
        is_above = !is_above;
        current = current->next.get();
    }

    if (is_above != starting_above) {
        const auto sign = cross_product_sign_in_clipper_range(
            current->prev->pt, current->pt, point);
        if (sign == 0) { return PointInPolygonResult::IsOn; }
        if ((sign < 0) == is_above) { value = 1 - value; }
    }

    return value == 0 ? PointInPolygonResult::IsOutside : PointInPolygonResult::IsInside;
}

}  // namespace clipper2next::internal
