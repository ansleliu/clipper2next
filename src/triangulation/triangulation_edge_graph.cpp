#include "triangulation/private/triangulation_edge_graph.h"

namespace clipper2next::internal {

auto make_triangulation_edge(triangulation_vertex* first,
                             triangulation_vertex* second,
                             triangulation_edge_kind kind) -> triangulation_edge {
    triangulation_edge edge;
    initialize_triangulation_edge(edge, first, second, kind);
    return edge;
}

void initialize_triangulation_edge(triangulation_edge& edge,
                                   triangulation_vertex* first,
                                   triangulation_vertex* second,
                                   triangulation_edge_kind kind) {
    edge = triangulation_edge{};
    edge.first = first;
    edge.second = second;

    if (first->pt.y == second->pt.y) {
        edge.vB = first;
        edge.vT = second;
    } else if (first->pt.y < second->pt.y) {
        edge.vB = second;
        edge.vT = first;
    } else {
        edge.vB = first;
        edge.vT = second;
    }

    if (first->pt.x <= second->pt.x) {
        edge.vL = first;
        edge.vR = second;
    } else {
        edge.vL = second;
        edge.vR = first;
    }

    edge.kind = kind;
}

}  // namespace clipper2next::internal
