#include "rectclip/private/rectclip_edges.h"

#include <cstddef>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto is_heading_clockwise(const Point64& first, const Point64& second, int edge_index)
    -> bool {
    switch (edge_index) {
    case 0: {
        return second.y < first.y;
    }
    case 1: {
        return second.x > first.x;
    }
    case 2: {
        return second.y > first.y;
    }
    default: {
        return second.x < first.x;
    }
    }
}

[[nodiscard]] auto unlink_node_back(rectclip_node& node) -> rectclip_node* {
    if (node.next == &node) { return nullptr; }
    auto& previous = *node.prev;
    auto& next = *node.next;
    previous.next = next;
    next.prev = previous;
    return &previous;
}

auto maybe_attach_boundary_edge(rectclip_node_list* edges,
                                rectclip_node& node,
                                rectclip_node* node_ptr,
                                std::uint32_t previous_edges,
                                std::uint32_t current_edges) -> void {
    if (!current_edges || node.edge) { return; }
    const auto combined_set = previous_edges & current_edges;
    for (int edge_index = 0; edge_index < 4; ++edge_index) {
        if (!(combined_set & (1U << edge_index))) { continue; }
        const auto target = static_cast<std::size_t>(edge_index) * 2U;
        if (is_heading_clockwise((*node.prev).pt, node.pt, edge_index)) {
            add_to_edge(edges[target], node_ptr);
        } else {
            add_to_edge(edges[target + 1U], node_ptr);
        }
    }
}

}  // namespace

auto check_edges(rectclip_node_list& results, rectclip_node_list* edges, const Rect64& rect)
    -> void {
    for (std::size_t i = 0; i < results.size(); ++i) {
        rectclip_node* node = results[i].get();
        if (!node) { continue; }
        rectclip_node* cursor = node;
        do {
            auto& current = *cursor;
            if (is_collinear((*current.prev).pt, current.pt, (*current.next).pt)) {
                cursor = unlink_node_back(current);
                if (!cursor) { break; }
                if (&current == node) { node = (*cursor).prev.get(); }
            } else {
                cursor = current.next.get();
            }
        } while (cursor != node);

        if (!cursor) {
            results[i] = nullptr;
            continue;
        }
        results[i] = node;

        auto previous_edges = get_edges_for_point((*(*node).prev).pt, rect);
        cursor = node;
        do {
            auto& current = *cursor;
            const auto current_edges = get_edges_for_point(current.pt, rect);
            maybe_attach_boundary_edge(edges, current, cursor, previous_edges, current_edges);
            previous_edges = current_edges;
            cursor = current.next.get();
        } while (cursor != node);
    }
}

}  // namespace clipper2next::internal
