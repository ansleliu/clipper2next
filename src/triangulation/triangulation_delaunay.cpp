#include "triangulation/private/triangulation_delaunay.h"

#include "triangulation/private/triangulation_legalizer.h"
#include "triangulation/private/triangulation_topology.h"

#include <cstddef>

namespace clipper2next::internal {

auto force_triangulation_edge_legal(triangulation_context& context, triangulation_edge* edge)
    -> bool {
    if (context.internal_error || edge == nullptr) { return false; }
    if (!(*edge).triA || !(*edge).triB) { return true; }

    triangulation_vertex* vertex_a = nullptr;
    triangulation_vertex* vertex_b = nullptr;
    triangulation_edge* edges_a[3] = {nullptr, nullptr, nullptr};
    triangulation_edge* edges_b[3] = {nullptr, nullptr, nullptr};

    for (int index = 0; index < 3; ++index) {
        auto* triangle_edge = (*(*edge).triA).edges[index].get();
        if (triangle_edge == edge) { continue; }
        switch (edge_contains(triangle_edge, (*edge).vL)) {
        case triangulation_edge_contains_result::left: {
            edges_a[1] = triangle_edge;
            vertex_a = (*triangle_edge).vR.get();
            break;
        }
        case triangulation_edge_contains_result::right: {
            edges_a[1] = triangle_edge;
            vertex_a = (*triangle_edge).vL.get();
            break;
        }
        default: {
            edges_b[1] = triangle_edge;
            break;
        }
        }
    }

    for (int index = 0; index < 3; ++index) {
        auto* triangle_edge = (*(*edge).triB).edges[index].get();
        if (triangle_edge == edge) { continue; }
        switch (edge_contains(triangle_edge, (*edge).vL)) {
        case triangulation_edge_contains_result::left: {
            edges_a[2] = triangle_edge;
            vertex_b = (*triangle_edge).vR.get();
            break;
        }
        case triangulation_edge_contains_result::right: {
            edges_a[2] = triangle_edge;
            vertex_b = (*triangle_edge).vL.get();
            break;
        }
        default: {
            edges_b[2] = triangle_edge;
            break;
        }
        }
    }

    const auto left_point = (*(*edge).vL).pt;
    const auto right_point = (*(*edge).vR).pt;
    if (vertex_a == nullptr || vertex_b == nullptr) {
        mark_triangulation_internal_error(context);
        return false;
    }
    const auto orientation_a = cross_product_sign((*vertex_a).pt, left_point, right_point);
    if (orientation_a == 0) { return true; }

    const auto in_circle = InCircleTest((*vertex_a).pt, left_point, right_point, (*vertex_b).pt);
    const bool right_turning = orientation_a > 0;
    if (in_circle == 0 || (right_turning == (in_circle < 0))) { return true; }

    (*edge).vL = vertex_a;
    (*edge).vR = vertex_b;

    (*(*edge).triA).edges[0] = edge;
    for (int index = 1; index < 3; ++index) {
        (*(*edge).triA).edges[index] = edges_a[index];
        if (!edges_a[index]) {
            mark_triangulation_internal_error(context);
            return false;
        }
        if (is_loose_edge(*edges_a[index])) { enqueue_pending_delaunay(context, edges_a[index]); }
        if (edges_a[index]->triA == (*edge).triA || edges_a[index]->triB == (*edge).triA) {
            continue;
        }

        if (edges_a[index]->triA == (*edge).triB) {
            edges_a[index]->triA = (*edge).triA;
        } else if (edges_a[index]->triB == (*edge).triB) {
            edges_a[index]->triB = (*edge).triA;
        } else {
            mark_triangulation_internal_error(context);
            return false;
        }
    }

    (*(*edge).triB).edges[0] = edge;
    for (int index = 1; index < 3; ++index) {
        (*(*edge).triB).edges[index] = edges_b[index];
        if (!edges_b[index]) {
            mark_triangulation_internal_error(context);
            return false;
        }
        if (is_loose_edge(*edges_b[index])) { enqueue_pending_delaunay(context, edges_b[index]); }
        if (edges_b[index]->triA == (*edge).triB || edges_b[index]->triB == (*edge).triB) {
            continue;
        }

        if (edges_b[index]->triA == (*edge).triA) {
            edges_b[index]->triA = (*edge).triB;
        } else if (edges_b[index]->triB == (*edge).triA) {
            edges_b[index]->triB = (*edge).triB;
        } else {
            mark_triangulation_internal_error(context);
            return false;
        }
    }
    return !context.internal_error;
}

auto legalize_pending_delaunay_edges(triangulation_context& context) -> bool {
    if (context.internal_error) { return false; }
    if (!context.use_delaunay) {
        context.pending_delaunay.clear();
        return true;
    }

    while (!context.pending_delaunay.empty()) {
        auto* edge = context.pending_delaunay.back().get();
        context.pending_delaunay.pop_back();
        if (!force_triangulation_edge_legal(context, edge)) { return false; }
    }
    return !context.internal_error;
}

}  // namespace clipper2next::internal
