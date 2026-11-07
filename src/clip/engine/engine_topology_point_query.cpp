#include "clip/engine/private/engine_topology.h"

#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {

auto points_in_output_polygon(const std::array<Point64, 2>& points,
                               output_point_node* output_point)
    -> std::array<PointInPolygonResult, 2> {
    using result = PointInPolygonResult;
    if (output_point == output_point->next || output_point->prev == output_point->next) {
        return {result::IsOutside, result::IsOutside};
    }
    std::array<bool, 2> inside{}, on{};
    auto previous_y = output_point->prev->pt.y;
    std::array<bool, 2> off_level{previous_y != points[0].y, previous_y != points[1].y};
    const auto low = (std::min)(points[0].y, points[1].y);
    const auto high = (std::max)(points[0].y, points[1].y);
    auto* current_node = output_point;
    do {
        // An edge wholly below or above both query levels cannot cross
        // either ray or contain either point. Share the original skip walk.
        if (current_node != output_point && previous_y < low) {
            while (current_node != output_point && current_node->pt.y < low) {
                current_node = current_node->next.get();
            }
            if (current_node == output_point) { break; }
        } else if (current_node != output_point && previous_y > high) {
            while (current_node != output_point && current_node->pt.y > high) {
                current_node = current_node->next.get();
            }
            if (current_node == output_point) { break; }
        }
        const auto& previous = current_node->prev->pt;
        const auto& current = current_node->pt;
        for (std::size_t i = 0; i != points.size(); ++i) {
            const auto& point = points[i];
            off_level[i] = off_level[i] || current.y != point.y;
            if (on[i]) { continue; }
            if (current.y == point.y &&
                (current.x == point.x ||
                 (previous.y == point.y &&
                  (point.x < previous.x) != (point.x < current.x)))) {
                on[i] = true;
                continue;
            }
            if ((previous.y > point.y) == (current.y > point.y)) { continue; }
            if (point.x < previous.x && point.x < current.x) {
                inside[i] = !inside[i];
            } else if (!(point.x > previous.x && point.x > current.x)) {
                const auto sign = cross_product_sign_in_clipper_range(previous, current, point);
                if (!sign) { on[i] = true; }
                else if ((sign > 0) == (current.y > previous.y)) { inside[i] = !inside[i]; }
            }
        }
        previous_y = current.y;
        current_node = current_node->next.get();
    } while (current_node != output_point);
    std::array<result, 2> results;
    for (std::size_t i = 0; i != results.size(); ++i) {
        // Preserve the established all-horizontal degenerate-ring result.
        results[i] = !off_level[i] ? result::IsOutside :
                     on[i] ? result::IsOn : inside[i] ? result::IsInside : result::IsOutside;
    }
    return results;
}

}  // namespace clipper2next::internal
