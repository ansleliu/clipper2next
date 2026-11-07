#include "clip/engine/private/engine_topology.h"

namespace clipper2next::internal {

auto get_clean_path(output_point_node* output_point, scoped_workspace_charge& charge) -> Path64 {
    Path64 result;
    const auto append = [&](const Point64& point) {
        auto growth = scoped_workspace_growth{charge, checked_size_add(result.size(), 1U), sizeof(Point64)};
        result.push_back(point);
        growth.commit();
    };
    auto* current = output_point;
    while (current->next != output_point &&
           ((current->pt.x == current->next->pt.x && current->pt.x == current->prev->pt.x) ||
            (current->pt.y == current->next->pt.y && current->pt.y == current->prev->pt.y))) {
        current = current->next.get();
    }
    append(current->pt);
    auto* previous = current;
    current = current->next.get();
    while (current != output_point) {
        if ((current->pt.x != current->next->pt.x || current->pt.x != previous->pt.x) &&
            (current->pt.y != current->next->pt.y || current->pt.y != previous->pt.y)) {
            append(current->pt);
            previous = current;
        }
        current = current->next.get();
    }
    return result;
}

}  // namespace clipper2next::internal
