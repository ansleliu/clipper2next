#include "triangulation/private/triangulation_boundary_sector.h"
#include "triangulation/private/triangulation_sweep_triangulators.h"
#include "geometry/private/geometry_predicates.h"
#include "triangulation/private/triangulation_topology.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include <cassert>

namespace clipper2next::internal {
namespace {
auto triangulate_side(const triangulation_sweep_front& front,
                      triangulation_edge* edge,
                      triangulation_vertex* pivot,
                      bool left) -> void {
    auto& context = front.context();
    const auto minimum = front.minimum();
    if (context.internal_error || edge_completed(edge)) { return; }
    auto* vertex = edge->vB == pivot ? edge->vT.get() : edge->vB.get();
    const auto owner = endpoint_event(*edge, pivot);
    triangulation_vertex* alternate = nullptr;
    triangulation_edge* alternate_edge = nullptr;
    triangulation_edge* link = nullptr;
    std::optional<triangulation_sweep_diagonal> diagonal;
    for (auto candidate_ref : pivot->edges) {
        auto* candidate = candidate_ref.get();
        if (candidate == edge || !candidate->isActive || edge_completed(candidate) ||
            endpoint_event(*candidate, pivot) != owner) {
            continue;
        }
        auto* point = candidate->vB == pivot ? candidate->vT.get() : candidate->vB.get();
        if (point == vertex || sweep_before(minimum, point->pt)) { continue; }
        const auto cross = front.numeric_domain().orientation(vertex->pt, pivot->pt, point->pt);
        if (left ? cross >= 0 : cross <= 0) { continue; }
        if (alternate) {
            const auto turn =
                front.numeric_domain().orientation(point->pt, pivot->pt, alternate->pt);
            if (left ? turn >= 0 : turn <= 0) { continue; }
        }
        if (sweep_before(pivot->pt, point->pt)) {
            if (left ? is_left_edge(*candidate) : is_right_edge(*candidate)) { continue; }
        } else if (left ? is_right_edge(*candidate) : is_left_edge(*candidate)) {
            continue;
        }
        const auto va = endpoint_event(*edge, vertex), vb = endpoint_event(*candidate, point);
        if (owner != no_boundary_event &&
            (!context.boundary_events[owner].sector.contains_triangle(vertex->pt, point->pt) ||
             !context.boundary_events[va].sector.contains_triangle(pivot->pt, point->pt) ||
             !context.boundary_events[vb].sector.contains_triangle(pivot->pt, vertex->pt))) {
            continue;
        }
        auto* candidate_link = find_linking_edge(point, vertex, vb, va);
        std::optional<triangulation_sweep_diagonal> proof;
        if (!candidate_link) {
            auto proposed = triangulation_sweep_diagonal::probe(front, point, vertex);
            if (!proposed) { continue; }
            proof.emplace(std::move(*proposed));
        }
        alternate = point;
        alternate_edge = candidate;
        link = candidate_link;
        diagonal.reset();
        if (proof) { diagonal.emplace(std::move(*proof)); }
    }
    if (!alternate) { return; }
    if (!link) {
        assert(diagonal && diagonal->first() == alternate && diagonal->second() == vertex);
        link = create_sweep_loose_edge(std::move(*diagonal));
    }
    if (!link) {
        mark_triangulation_internal_error(context);
        return;
    }
    if (!create_sweep_triangle(context, edge, alternate_edge, link, front.numeric_domain())) { return; }
    if (!edge_completed(link)) { triangulate_side(front, link, alternate, left); }
}
}
auto triangulate_left(const triangulation_sweep_front& front,
                      triangulation_edge* edge,
                      triangulation_vertex* pivot) -> void {
    triangulate_side(front, edge, pivot, true);
}
auto triangulate_right(const triangulation_sweep_front& front,
                       triangulation_edge* edge,
                       triangulation_vertex* pivot) -> void {
    triangulate_side(front, edge, pivot, false);
}
}
