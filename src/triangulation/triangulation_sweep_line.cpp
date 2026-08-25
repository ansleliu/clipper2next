#include "triangulation/private/triangulation_sweep_line.h"

#include "triangulation/private/triangulation_graph_repair.h"
#include "triangulation/private/triangulation_intersections.h"
#include "triangulation/private/triangulation_legalizer.h"
#include "triangulation/private/triangulation_topology.h"
#include "triangulation/private/triangulation_sweep_triangulators.h"

#include <algorithm>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto vertex_sort(triangulation_vertex_ref first, triangulation_vertex_ref second)
    -> bool {
    return ((*first).pt.y == (*second).pt.y) ? ((*first).pt.x < (*second).pt.x)
                                             : ((*first).pt.y > (*second).pt.y);
}

[[nodiscard]] auto edge_sort(triangulation_edge_ref first, triangulation_edge_ref second) -> bool {
    return (*(*first).vL).pt.x < (*(*second).vL).pt.x;
}

[[nodiscard]] auto create_inner_local_minimum_edge(triangulation_context& context,
                                                   triangulation_vertex* upper_vertex)
    -> triangulation_edge* {
    if (!context.first_active) { return nullptr; }

    const auto upper_x = (*upper_vertex).pt.x;
    const auto upper_y = (*upper_vertex).pt.y;
    auto* edge = context.first_active.get();
    triangulation_edge* below_edge = nullptr;
    double best_distance = -1.0;
    while (edge) {
        if ((*(*edge).vL).pt.x <= upper_x && (*(*edge).vR).pt.x >= upper_x &&
            (*(*edge).vB).pt.y >= upper_y && (*edge).vB != upper_vertex &&
            (*edge).vT != upper_vertex &&
            !LeftTurning((*(*edge).vL).pt, (*upper_vertex).pt, (*(*edge).vR).pt)) {
            const auto distance =
                ShortestDistFromSegment((*upper_vertex).pt, (*(*edge).vL).pt, (*(*edge).vR).pt);
            if (!below_edge || distance < best_distance) {
                below_edge = edge;
                best_distance = distance;
            }
        }
        edge = (*edge).nextE.get();
    }
    if (!below_edge) { return nullptr; }

    auto* best_vertex =
        ((*(*below_edge).vT).pt.y <= upper_y) ? (*below_edge).vB.get() : (*below_edge).vT.get();
    auto best_x = (*best_vertex).pt.x;
    auto best_y = (*best_vertex).pt.y;

    edge = context.first_active.get();
    if (best_x < upper_x) {
        while (edge) {
            if ((*(*edge).vR).pt.x > best_x && (*(*edge).vL).pt.x < upper_x &&
                (*(*edge).vB).pt.y > upper_y && (*(*edge).vT).pt.y < best_y &&
                SegsIntersect(
                    (*(*edge).vB).pt, (*(*edge).vT).pt, (*best_vertex).pt, (*upper_vertex).pt) ==
                    triangulation_intersect_kind::intersect) {
                best_vertex = ((*(*edge).vT).pt.y > upper_y) ? (*edge).vT.get() : (*edge).vB.get();
                best_x = (*best_vertex).pt.x;
                best_y = (*best_vertex).pt.y;
            }
            edge = (*edge).nextE.get();
        }
    } else {
        while (edge) {
            if ((*(*edge).vR).pt.x < best_x && (*(*edge).vL).pt.x > upper_x &&
                (*(*edge).vB).pt.y > upper_y && (*(*edge).vT).pt.y < best_y &&
                SegsIntersect(
                    (*(*edge).vB).pt, (*(*edge).vT).pt, (*best_vertex).pt, (*upper_vertex).pt) ==
                    triangulation_intersect_kind::intersect) {
                best_vertex = (*(*edge).vT).pt.y > upper_y ? (*edge).vT.get() : (*edge).vB.get();
                best_x = (*best_vertex).pt.x;
                best_y = (*best_vertex).pt.y;
            }
            edge = (*edge).nextE.get();
        }
    }
    return create_sweep_loose_edge(context, best_vertex, upper_vertex);
}

[[nodiscard]] auto prepare_sweep(triangulation_context& context) -> TriangulateResult {
    if (context.lowermost_vertex->innerLM) {
        while (!context.local_minima.empty()) {
            context.local_minima.back()->innerLM = !context.local_minima.back()->innerLM;
            context.local_minima.pop_back();
        }
        for (auto edge_ref : context.edges) {
            auto& edge = *edge_ref;
            if (edge.kind == triangulation_edge_kind::ascend) {
                edge.kind = triangulation_edge_kind::descend;
            } else if (edge.kind == triangulation_edge_kind::descend) {
                edge.kind = triangulation_edge_kind::ascend;
            }
        }
    } else {
        context.local_minima.clear();
    }

    std::sort(context.edges.begin(), context.edges.end(), edge_sort);
    if (!repair_triangulation_graph(context)) { return TriangulateResult::paths_intersect; }
    if (context.internal_error) { return TriangulateResult::fail; }
    std::sort(context.vertices.begin(), context.vertices.end(), vertex_sort);
    merge_duplicate_or_collinear_vertices(context);
    return context.internal_error ? TriangulateResult::fail : TriangulateResult::success;
}

[[nodiscard]] auto process_local_minima(triangulation_context& context, int64_t current_y)
    -> bool {
    while (!context.local_minima.empty()) {
        auto* local_minimum = context.local_minima.back().get();
        context.local_minima.pop_back();
        auto* edge = create_inner_local_minimum_edge(context, local_minimum);
        if (!edge) { return false; }

        if (is_horizontal_edge(*edge)) {
            if (edge->vL == edge->vB) {
                triangulate_left(context, edge, edge->vB.get(), current_y);
            } else {
                triangulate_right(context, edge, edge->vB.get(), current_y);
            }
        } else {
            triangulate_left(context, edge, edge->vB.get(), current_y);
            if (!edge_completed(edge) && !context.internal_error) {
                triangulate_right(context, edge, edge->vB.get(), current_y);
            }
        }
        if (context.internal_error) { return false; }
        add_sweep_active_edge(context, local_minimum->edges[0].get());
        add_sweep_active_edge(context, local_minimum->edges[1].get());
    }
    return true;
}

[[nodiscard]] auto process_horizontal_edges(triangulation_context& context, int64_t current_y)
    -> bool {
    while (!context.horizontal_edges.empty()) {
        auto* edge = context.horizontal_edges.back().get();
        context.horizontal_edges.pop_back();
        if (edge_completed(edge)) { continue; }
        if (edge->vB == edge->vL) {
            if (is_left_edge(*edge)) {
                triangulate_left(context, edge, edge->vB.get(), current_y);
            }
        } else if (is_right_edge(*edge)) {
            triangulate_right(context, edge, edge->vB.get(), current_y);
        }
        if (context.internal_error) { return false; }
    }
    return true;
}

[[nodiscard]] auto process_vertex_edges(triangulation_context& context,
                                        triangulation_vertex& vertex) -> bool {
    for (auto index = vertex.edges.size(); index-- > 0U;) {
        if (index >= vertex.edges.size()) { continue; }
        auto* edge = vertex.edges[index].get();
        if (edge_completed(edge) || is_loose_edge(*edge)) { continue; }

        if (&vertex == edge->vB.get()) {
            if (is_horizontal_edge(*edge)) { context.horizontal_edges.push_back(edge); }
            if (!vertex.innerLM) { add_sweep_active_edge(context, edge); }
        } else if (is_horizontal_edge(*edge)) {
            context.horizontal_edges.push_back(edge);
        } else if (is_left_edge(*edge)) {
            triangulate_left(context, edge, edge->vB.get(), vertex.pt.y);
        } else {
            triangulate_right(context, edge, edge->vB.get(), vertex.pt.y);
        }
        if (context.internal_error) { return false; }
    }
    return true;
}

[[nodiscard]] auto finish_horizontal_edges(triangulation_context& context, int64_t current_y)
    -> bool {
    while (!context.horizontal_edges.empty()) {
        auto* edge = context.horizontal_edges.back().get();
        context.horizontal_edges.pop_back();
        if (!edge_completed(edge) && edge->vB == edge->vL) {
            triangulate_left(context, edge, edge->vB.get(), current_y);
            if (context.internal_error) { return false; }
        }
    }
    return !context.internal_error;
}

}  // namespace

auto run_triangulation_sweep(triangulation_context& context) -> TriangulateResult {
    if (!context.lowermost_vertex) { return TriangulateResult::no_polygons; }
    if (const auto status = prepare_sweep(context); status != TriangulateResult::success) {
        return status;
    }

    auto current_y = context.vertices.front()->pt.y;
    for (auto vertex_ref : context.vertices) {
        auto* vertex = vertex_ref.get();
        if (vertex->edges.empty()) { continue; }
        if (vertex->pt.y != current_y) {
            if (!process_local_minima(context, current_y) ||
                !process_horizontal_edges(context, current_y)) {
                return TriangulateResult::fail;
            }
            current_y = vertex->pt.y;
        }
        if (!process_vertex_edges(context, *vertex)) { return TriangulateResult::fail; }
        if (vertex->innerLM) { context.local_minima.push_back(vertex); }
    }
    return finish_horizontal_edges(context, current_y) ? TriangulateResult::success
                                                       : TriangulateResult::fail;
}

}  // namespace clipper2next::internal
