#include "triangulation/private/triangulation_sweep.h"

#include "triangulation/private/triangulation_topology.h"

namespace clipper2next::internal {

auto add_sweep_active_edge(triangulation_context& context, triangulation_edge* edge) -> void {
    if (edge->isActive) { return; }

    edge->prevE = nullptr;
    edge->nextE = context.first_active;
    edge->isActive = true;
    if (context.first_active) { context.first_active->prevE = edge; }
    context.first_active = edge;
}

auto remove_sweep_active_edge(triangulation_context& context, triangulation_edge* edge) -> void {
    if (!remove_edge_from_vertex(context, edge->vB, edge)) { return; }
    if (!remove_edge_from_vertex(context, edge->vT, edge)) { return; }

    auto* previous = edge->prevE.get();
    auto* next = edge->nextE.get();
    if (next) { next->prevE = previous; }
    if (previous) { previous->nextE = next; }
    edge->isActive = false;
    if (context.first_active == edge) { context.first_active = next; }
}

auto create_sweep_loose_edge(triangulation_context& context,
                             triangulation_vertex* first,
                             triangulation_vertex* second) -> triangulation_edge* {
    auto* edge = create_triangulation_edge(context, first, second, triangulation_edge_kind::loose);
    enqueue_pending_delaunay(context, edge);
    add_sweep_active_edge(context, edge);
    return edge;
}

auto create_sweep_triangle(triangulation_context& context,
                           triangulation_edge* first,
                           triangulation_edge* second,
                           triangulation_edge* third) -> triangulation_triangle* {
    if (context.internal_error) { return nullptr; }
    auto* triangle = create_triangulation_triangle(context, first, second, third);
    for (auto edge_ref : triangle->edges) {
        auto* edge = edge_ref.get();
        if (edge->triA) {
            edge->triB = triangle;
            remove_sweep_active_edge(context, edge);
            if (context.internal_error) { return nullptr; }
        } else {
            edge->triA = triangle;
            if (!is_loose_edge(*edge)) {
                remove_sweep_active_edge(context, edge);
                if (context.internal_error) { return nullptr; }
            }
        }
    }
    return triangle;
}

auto add_triangulation_active_edge(triangulation_context& context, triangulation_edge* edge)
    -> void {
    add_sweep_active_edge(context, edge);
}

auto remove_triangulation_active_edge(triangulation_context& context, triangulation_edge* edge)
    -> void {
    remove_sweep_active_edge(context, edge);
}

}  // namespace clipper2next::internal
