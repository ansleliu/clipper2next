#include "rectclip/private/rectclip_edges.h"

#include <cstddef>

namespace clipper2next::internal {

auto add_to_edge(rectclip_node_list& edge, rectclip_node* node) -> void {
    auto& node_ref = *node;
    if (node_ref.edge) { return; }
    node_ref.edge = edge;
    edge.emplace_back(node);
}

auto uncouple_edge(rectclip_node* node) -> void {
    auto& node_ref = *node;
    if (!node_ref.edge) { return; }
    for (std::size_t i = 0; i < node_ref.edge->size(); ++i) {
        rectclip_node* edge_node = (*node_ref.edge)[i].get();
        if (edge_node == node) {
            (*node_ref.edge)[i] = nullptr;
            break;
        }
    }
    node_ref.edge = nullptr;
}

auto set_new_owner(rectclip_node* node, std::size_t new_index) -> void {
    auto& origin = *node;
    origin.owner_index = new_index;
    rectclip_node* next = origin.next.get();
    while (next != node) {
        auto& next_node = *next;
        next_node.owner_index = new_index;
        next = next_node.next.get();
    }
}

auto get_edges_for_point(const Point64& point, const Rect64& rect) -> std::uint32_t {
    std::uint32_t result = 0;
    if (point.x == rect.left) {
        result = 1;
    } else if (point.x == rect.right) {
        result = 4;
    }
    if (point.y == rect.top) {
        result += 2;
    } else if (point.y == rect.bottom) {
        result += 8;
    }
    return result;
}

}  // namespace clipper2next::internal
