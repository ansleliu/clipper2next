#include "clip/engine/private/engine_topology.h"

#include "clipper2next/geometry/algorithms.h"
#include "geometry/private/geometry_predicates.h"

#include <algorithm>
#include <cstdint>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto doubled(const Point64 point) noexcept -> Point64 {
    // Engine output coordinates have already passed the Clipper range guard
    // (INT64_MAX / 4), so doubling remains exactly representable in int64_t.
    return Point64{point.x * 2, point.y * 2};
}

[[nodiscard]] auto between_closed(
    const std::int64_t value,
    const std::int64_t first,
    const std::int64_t second) noexcept -> bool {
    return value >= (std::min)(first, second) &&
        value <= (std::max)(first, second);
}

[[nodiscard]] auto point_in_doubled_output_polygon(
    const Point64 point,
    output_point_node* polygon) -> PointInPolygonResult {
    auto inside = false;
    auto previous = doubled(polygon->prev->pt);
    auto* current_node = polygon;
    do {
        const auto current = doubled(current_node->pt);
        const auto orientation =
            clipper2next::internal::cross_product_sign(previous, current, point);
        if (orientation == 0 &&
            between_closed(point.x, previous.x, current.x) &&
            between_closed(point.y, previous.y, current.y)) {
            return PointInPolygonResult::IsOn;
        }

        const auto current_above = current.y > point.y;
        if ((previous.y > point.y) != current_above &&
            (orientation > 0) == current_above) {
            inside = !inside;
        }
        previous = current;
        current_node = current_node->next.get();
    } while (current_node != polygon);
    return inside ? PointInPolygonResult::IsInside
                  : PointInPolygonResult::IsOutside;
}

[[nodiscard]] auto boundary_path_is_inside(
    output_point_node* inner,
    output_point_node* outer) -> bool {
    auto* current = inner;
    do {
        const auto& first = current->pt;
        const auto& second = current->next->pt;
        const auto exact_midpoint_on_doubled_lattice =
            Point64{first.x + second.x, first.y + second.y};
        switch (point_in_doubled_output_polygon(
            exact_midpoint_on_doubled_lattice, outer)) {
        case PointInPolygonResult::IsInside:
            return true;
        case PointInPolygonResult::IsOutside:
            return false;
        case PointInPolygonResult::IsOn:
            break;
        }
        current = current->next.get();
    } while (current != inner);
    return false;
}

}  // namespace

auto path2_contains_path1(output_point_node* first, output_point_node* second) -> bool {
    auto result = PointInPolygonResult::IsOn;
    auto* current = first;
    do {
        switch (point_in_output_polygon(current->pt, second)) {
        case PointInPolygonResult::IsOutside: {
            if (result == PointInPolygonResult::IsOutside) { return false; }
            result = PointInPolygonResult::IsOutside;
            break;
        }
        case PointInPolygonResult::IsInside: {
            if (result == PointInPolygonResult::IsInside) { return true; }
            result = PointInPolygonResult::IsInside;
            break;
        }
        default: {
            break;
        }
        }
        current = current->next.get();
    } while (current != first);

    if (result == PointInPolygonResult::IsOn) {
        return boundary_path_is_inside(first, second);
    }
    return clipper2next::path_contains_path(get_clean_path(first), get_clean_path(second));
}

auto move_splits(output_record_node* from, output_record_node* to) -> void {
    for (auto split_ref : from->splits) {
        auto* split = split_ref.get();
        if (to != split) { to->splits.emplace_back(split); }
    }
    from->splits.clear();
}

}  // namespace clipper2next::internal
