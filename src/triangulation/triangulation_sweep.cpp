#include "triangulation/private/triangulation_boundary_sector.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include <cassert>

#include "triangulation/private/triangulation_topology.h"
#include "geometry/private/geometry_predicates.h"
#include "geometry/private/geometry_segment_query.h"

namespace clipper2next::internal {
namespace {
struct sweep_triangle_cycle final {
    triangulation_vertex* a;
    triangulation_vertex* b;
    triangulation_vertex* c;
    triangulation_edge* ac;
    triangulation_edge* bc;
};

auto connects(const triangulation_edge& edge, const triangulation_vertex* a,
              const triangulation_vertex* b) noexcept -> bool {
    return (edge.first == a && edge.second == b) || (edge.first == b && edge.second == a);
}

auto triangle_cycle(triangulation_edge* ab, triangulation_edge* other,
                    triangulation_edge* last) -> std::optional<sweep_triangle_cycle> {
    if (ab == other || ab == last || other == last) { return std::nullopt; }
    auto* a = ab->first.get();
    auto* b = ab->second.get();
    if (!a || !b || a == b) { return std::nullopt; }
    triangulation_vertex* c = nullptr;
    if (other->first == a || other->first == b) { c = other->second.get(); }
    else if (other->second == a || other->second == b) { c = other->first.get(); }
    if (!c || c == a || c == b) { return std::nullopt; }
    auto* ac = connects(*other, a, c) ? other : last;
    auto* bc = ac == other ? last : other;
    if (!connects(*ac, a, c) || !connects(*bc, b, c)) { return std::nullopt; }
    return sweep_triangle_cycle{a, b, c, ac, bc};
}

auto corner_is_compatible(const triangulation_context& context,
                          triangulation_vertex* vertex,
                          const triangulation_edge& first,
                          const triangulation_edge& second,
                          Point64 a, Point64 b) -> bool {
    const auto owner = endpoint_event(first, vertex);
    if (owner != endpoint_event(second, vertex)) { return false; }
    return owner == no_boundary_event || context.boundary_events[owner].sector.contains_triangle(a, b);
}
}  // namespace


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

auto triangulation_sweep_diagonal::probe(const triangulation_sweep_front& front,
                                         triangulation_vertex* first,
                                         triangulation_vertex* second)
    -> std::optional<triangulation_sweep_diagonal> {
    const auto a = first->pt, b = second->pt;
    assert(front.includes(a) && front.includes(b));
    if (a == b) { return std::nullopt; }
    const geometry_segment_query query{a,b,front.numeric_domain().orientation_fits_signed64()};
    for (auto edge = front.context().first_active; edge; edge = edge->nextE) {
        if (!edge->isActive) { continue; }
        const auto* c=edge->first.get();
        const auto* d=edge->second.get();
        const bool common_c=c==first || c==second;
        const bool common_d=d==first || d==second;
        if (common_c || common_d) {
            if ((!common_c && query.on_segment(c->pt)) ||
                (!common_d && query.on_segment(d->pt))) { return std::nullopt; }
            if (!is_loose_edge(*edge) && ((c==first && d==second) ||
                                        (c==second && d==first))) { return std::nullopt; }
        } else if (query.endpoint_or_crossing(c->pt,d->pt)) { return std::nullopt; }
    }
    return triangulation_sweep_diagonal{front, first, second};
}

auto create_sweep_loose_edge(triangulation_sweep_diagonal&& proof) -> triangulation_edge* {
    auto& context = proof.front_.context();
    auto* first = std::exchange(proof.first_, nullptr);
    auto* second = std::exchange(proof.second_, nullptr);
    if (!first || !second) {
        mark_triangulation_internal_error(context);
        return nullptr;
    }
    const auto first_id = event_for_ray(context, first, ray_to(first->pt, second->pt));
    const auto second_id = event_for_ray(context, second, ray_to(second->pt, first->pt));
    if (!context.boundary_events.empty() &&
        (first_id == no_boundary_event || second_id == no_boundary_event)) {
        return nullptr;
    }
    auto* edge = create_triangulation_edge(context, first, second, triangulation_edge_kind::loose);
    edge->first_event = first_id;
    edge->second_event = second_id;
    enqueue_pending_delaunay(context, edge);
    add_sweep_active_edge(context, edge);
    return edge;
}

auto create_sweep_triangle(triangulation_context& context,
                           triangulation_edge* first,
                           triangulation_edge* second,
                           triangulation_edge* third,
                            const triangulation_predicate_domain& domain) -> triangulation_triangle* {
    if (context.internal_error) { return nullptr; }
    if (!first || !second || !third || edge_completed(first) || edge_completed(second) ||
        edge_completed(third)) {
        mark_triangulation_internal_error(context);
        return nullptr;
    }
    const auto cycle = triangle_cycle(first, second, third);
    if (!cycle || !domain.orientation(cycle->a->pt, cycle->b->pt, cycle->c->pt) ||
        !corner_is_compatible(context, cycle->a, *first, *cycle->ac, cycle->b->pt, cycle->c->pt) ||
        !corner_is_compatible(context, cycle->b, *first, *cycle->bc, cycle->a->pt, cycle->c->pt) ||
        !corner_is_compatible(context, cycle->c, *cycle->ac, *cycle->bc, cycle->a->pt, cycle->b->pt)) {
        mark_triangulation_internal_error(context);
        return nullptr;
    }
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

}  // namespace clipper2next::internal
