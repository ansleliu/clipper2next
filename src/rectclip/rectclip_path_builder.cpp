#include "rectclip/private/rectclip_path_builder.h"

#include <cstddef>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto unlink_node(rectclip_node* node) -> rectclip_node* {
    if (node->next == node) { return nullptr; }
    node->prev->next = node->next;
    node->next->prev = node->prev;
    return node->next;
}

[[nodiscard]] auto materialize_node_ring(rectclip_node* start) -> Path64 {
    Path64 result;
    auto count = std::size_t{1U};
    for (auto* node = start->next.get(); node != start; node = node->next.get()) { ++count; }
    result.reserve(count);
    auto* node = start;
    do {
        result.emplace_back(node->pt);
        node = node->next.get();
    } while (node != start);
    return result;
}

}  // namespace

auto build_polygon_path(rectclip_node*& node) -> Path64 {
    if (!node || node->next == node->prev) { return {}; }

    rectclip_node* node2 = node->next;
    while (node2 && node2 != node) {
        if (is_collinear(node2->prev->pt, node2->pt, node2->next->pt)) {
            node = node2->prev;
            node2 = unlink_node(node2);
        } else {
            node2 = node2->next;
        }
    }
    node = node2;
    if (!node2) { return {}; }

    return materialize_node_ring(node);
}

auto build_line_path(rectclip_node*& node) -> Path64 {
    if (!node || node == node->next) { return {}; }
    node = node->next;
    return materialize_node_ring(node);
}

}  // namespace clipper2next::internal
