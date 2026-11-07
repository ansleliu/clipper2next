#include "triangulation/private/triangulation_edge_graph.h"
#include "triangulation/private/triangulation_boundary_events.h"

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
    edge.first = first;
    edge.second = second;

    if (sweep_before(first->pt, second->pt)) {
        edge.vB = first;
        edge.vT = second;
    } else {
        edge.vB = second;
        edge.vT = first;
    }

    if (first->pt.x <= second->pt.x) {
        edge.vL = first;
        edge.vR = second;
    } else {
        edge.vL = second;
        edge.vR = first;
    }

    edge.kind = kind;
    // Initialize the complete edge in place. Assigning a default aggregate
    // first materializes and copies every field before these endpoint writes.
    edge.triA = nullptr;
    edge.triB = nullptr;
    edge.first_event = no_boundary_event;
    edge.second_event = no_boundary_event;
    edge.isActive = false;
    edge.nextE = nullptr;
    edge.prevE = nullptr;
}

}  // namespace clipper2next::internal
