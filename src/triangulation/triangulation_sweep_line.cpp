#include "triangulation/private/triangulation_boundary_sector.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "triangulation/private/triangulation_exact_predicates.h"
#include "triangulation/private/triangulation_topology.h"
#include "triangulation/private/triangulation_sweep_triangulators.h"
#include <algorithm>
#include <cassert>

namespace clipper2next::internal {
namespace {
auto close_front(const triangulation_sweep_front& front, triangulation_edge* edge)
    -> void {
    if (edge_completed(edge)) { return; }
    if (is_left_edge(*edge)) {
        triangulate_left(front, edge, edge->vB.get());
    } else {
        triangulate_right(front, edge, edge->vB.get());
    }
}
auto bridge_minimum(const triangulation_sweep_front& phase, std::size_t event_id) -> bool {
    auto& context = phase.context();
    const auto& event = context.boundary_events[event_id];
    auto* vertex = event.vertex.get();
    const triangulation_sweep_front front{context, vertex->pt, phase.numeric_domain()};
    auto diagonal = nearest_sweep_diagonal(front, event_id);
    if (!diagonal) { return false; }
    auto* best = diagonal->first();
    auto* bridge = create_sweep_loose_edge(std::move(*diagonal));
    if (!bridge) { return false; }
    triangulate_left(front, bridge, best);
    if (!edge_completed(bridge)) { triangulate_right(front, bridge, best); }
    return !context.internal_error;
}
auto process_boundary_event(const triangulation_sweep_front& front, std::size_t event_id) -> bool {
    auto& context = front.context();
    const auto& event = context.boundary_events[event_id];
    const auto vertex = event.vertex;
    if (event.kind == boundary_event_kind::open) {
        if (event.reflex) { context.local_minima.push_back(event_id); }
        while (!context.local_minima.empty()) {
            const auto minimum = context.local_minima.back();
            context.local_minima.pop_back();
            if (!bridge_minimum(front, minimum)) { return false; }
        }
    } else {
        for (auto edge : {event.incoming, event.outgoing}) {
            if (edge->vT == vertex) { close_front(front, edge.get()); }
        }
    }
    if (context.internal_error) { return false; }
    for (auto edge : {event.incoming, event.outgoing}) {
        if (edge->vB == vertex && !edge_completed(edge.get())) {
            add_sweep_active_edge(context, edge.get());
        }
    }
    return !context.internal_error;
}
}  // namespace
auto nearest_sweep_diagonal(const triangulation_sweep_front& front, std::size_t event_id)
    -> std::optional<triangulation_sweep_diagonal> {
    auto& context = front.context();
    const auto& event = context.boundary_events[event_id];
    auto* vertex = event.vertex.get();
    assert(front.minimum() == vertex->pt);
    // The complete graph domain contains the minimum and every active endpoint.
    // Sweep creates edges and faces while this vertex snapshot remains fixed.
    const exact_point_distance_query distance{vertex->pt, front.numeric_domain().bounds()};
    exact_point_distance rejected_distance;
    triangulation_vertex* rejected = nullptr;
    // The read-only candidate set has a strict (distance, sweep point) order.
    // Only the first successful proof is retained and then consumed by creation.
    for (;;) {
        exact_point_distance best_distance;
        triangulation_vertex* best = nullptr;
        for (auto edge = context.first_active; edge; edge = edge->nextE) {
            for (auto endpoint : {edge->first, edge->second}) {
                auto* candidate = endpoint.get();
                if (!sweep_before(candidate->pt, vertex->pt) || candidate->edges.empty()) { continue; }
                const auto candidate_distance = distance.to_point(candidate->pt);
                if (rejected) {
                    const auto order = compare_squared_distance(candidate_distance, rejected_distance);
                    if (order < 0 || (order == 0 && !sweep_before(rejected->pt, candidate->pt))) { continue; }
                }
                if (best) {
                    const auto order = compare_squared_distance(candidate_distance, best_distance);
                    if (order > 0 || (order == 0 && !sweep_before(candidate->pt, best->pt))) { continue; }
                }
                if (!event.sector.contains_ray(ray_to(vertex->pt, candidate->pt), false)) { continue; }
                const auto target = event_for_ray(context, candidate, ray_to(candidate->pt, vertex->pt));
                if (target == no_boundary_event) { continue; }
                const bool active = std::any_of(candidate->edges.begin(), candidate->edges.end(), [&](auto incident) {
                    return incident->isActive && endpoint_event(*incident, candidate) == target;
                });
                if (!active) { continue; }
                best = candidate;
                best_distance = candidate_distance;
            }
        }
        if (!best) { return std::nullopt; }
        auto proof = triangulation_sweep_diagonal::probe(front, best, vertex);
        if (proof) { return proof; }
        rejected = best;
        rejected_distance = best_distance;
    }
}

auto run_triangulation_sweep(triangulation_context& context,
                             const triangulation_predicate_domain& domain) -> TriangulateResult {
    if (context.boundary_events.empty()) { return TriangulateResult::no_polygons; }
    std::sort(context.vertices.begin(), context.vertices.end(), [](auto a, auto b) {
        return sweep_before(a->pt, b->pt);
    });
    for (auto vertex : context.vertices) {
        const triangulation_sweep_front front{context, vertex->pt, domain};
        for (auto phase : {boundary_event_kind::close,
                           boundary_event_kind::continuation,
                           boundary_event_kind::open}) {
            for (auto id = vertex->event_begin; id < vertex->event_end; ++id) {
                const auto& event = context.boundary_events[id];
                if (event.kind != phase) { continue; }
                if (!process_boundary_event(front, id)) { return TriangulateResult::fail; }
            }
        }
    }
    for (auto edge : context.edges) {
        if (!edge_completed(edge.get())) { return TriangulateResult::fail; }
    }
    return context.internal_error ? TriangulateResult::fail : TriangulateResult::success;
}
}  // namespace clipper2next::internal
