#pragma once

#include "triangulation/private/triangulation_sweep_front.h"
#include "triangulation/private/triangulation_boundary_events.h"

#include <algorithm>

namespace clipper2next::internal {

[[nodiscard]] inline auto is_loose_edge(const triangulation_edge& edge) noexcept -> bool {
    return edge.kind == triangulation_edge_kind::loose;
}

[[nodiscard]] inline auto is_left_edge(const triangulation_edge& edge) noexcept -> bool {
    return edge.kind == triangulation_edge_kind::ascend;
}

[[nodiscard]] inline auto is_right_edge(const triangulation_edge& edge) noexcept -> bool {
    return edge.kind == triangulation_edge_kind::descend;
}

[[nodiscard]] inline auto is_horizontal_edge(const triangulation_edge& edge) noexcept -> bool {
    return edge.vB->pt.y == edge.vT->pt.y;
}

[[nodiscard]] inline auto edge_completed(const triangulation_edge* edge) noexcept -> bool {
    if (!edge->triA) { return false; }
    if (edge->triB) { return true; }
    return !is_loose_edge(*edge);
}

[[nodiscard]] inline auto edge_contains(const triangulation_edge* edge,
                                        const triangulation_vertex* vertex) noexcept
    -> triangulation_edge_contains_result {
    if (edge->vL == vertex) { return triangulation_edge_contains_result::left; }
    if (edge->vR == vertex) { return triangulation_edge_contains_result::right; }
    return triangulation_edge_contains_result::neither;
}

[[nodiscard]] inline auto remove_edge_from_vertex(triangulation_context& context,
                                                  triangulation_vertex* vertex,
                                                  triangulation_edge* edge) -> bool {
    const auto edge_iterator = std::find(vertex->edges.begin(), vertex->edges.end(), edge);
    if (edge_iterator == vertex->edges.end()) {
        mark_triangulation_internal_error(context);
        return false;
    }
    vertex->edges.erase(edge_iterator);
    return true;
}

[[nodiscard]] inline auto endpoint_event(const triangulation_edge& edge,
                                         const triangulation_vertex* vertex) noexcept
    -> std::size_t {
    return edge.first == vertex ? edge.first_event : edge.second_event;
}

[[nodiscard]] inline auto find_linking_edge(const triangulation_vertex* first,
                                            const triangulation_vertex* second,
                                            std::size_t first_event,
                                            std::size_t second_event) noexcept
    -> triangulation_edge* {
    for (auto edge_ref : first->edges) {
        auto* edge = edge_ref.get();
        if ((edge->first == second || edge->second == second) && !edge_completed(edge) &&
            endpoint_event(*edge, first) == first_event &&
            endpoint_event(*edge, second) == second_event) {
            return edge;
        }
    }
    return nullptr;
}

}  // namespace clipper2next::internal
