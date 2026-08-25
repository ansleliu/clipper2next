#include "clip/engine/private/engine_topology.h"

namespace clipper2next::internal {

auto get_clean_path(output_point_node* output_point) -> Path64 {
    Path64 result;
    auto* current = output_point;
    while (current->next != output_point &&
           ((current->pt.x == current->next->pt.x && current->pt.x == current->prev->pt.x) ||
            (current->pt.y == current->next->pt.y && current->pt.y == current->prev->pt.y))) {
        current = current->next.get();
    }
    result.emplace_back(current->pt);
    auto* previous = current;
    current = current->next.get();
    while (current != output_point) {
        if ((current->pt.x != current->next->pt.x || current->pt.x != previous->pt.x) &&
            (current->pt.y != current->next->pt.y || current->pt.y != previous->pt.y)) {
            result.emplace_back(current->pt);
            previous = current;
        }
        current = current->next.get();
    }
    return result;
}

}  // namespace clipper2next::internal
