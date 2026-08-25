#include "triangulation/private/triangulation_sweep_triangulators.h"

#include "triangulation/private/triangulation_intersections.h"
#include "triangulation/private/triangulation_legalizer.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "triangulation/private/triangulation_topology.h"

#include <algorithm>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto horizontal_between(const triangulation_context& context,
                                      const triangulation_vertex* first,
                                      const triangulation_vertex* second) -> triangulation_edge* {
    const auto y = (*first).pt.y;
    const auto left = std::min((*first).pt.x, (*second).pt.x);
    const auto right = std::max((*first).pt.x, (*second).pt.x);

    auto* result = context.first_active.get();
    while (result) {
        if ((*(*result).vL).pt.y == y && (*(*result).vR).pt.y == y &&
            (*(*result).vL).pt.x >= left && (*(*result).vR).pt.x <= right &&
            ((*(*result).vL).pt.x != left || (*(*result).vL).pt.x != right)) {
            break;
        }
        result = (*result).nextE.get();
    }
    return result;
}

}  // namespace

auto triangulate_left(triangulation_context& context,
                      triangulation_edge* edge,
                      triangulation_vertex* pivot,
                      int64_t minimum_y) -> void {
    if (context.internal_error) { return; }
    triangulation_vertex* alternate_vertex = nullptr;
    triangulation_edge* alternate_edge = nullptr;
    auto* vertex = ((*edge).vB == pivot) ? (*edge).vT.get() : (*edge).vB.get();

    for (auto candidate_edge_ref : (*pivot).edges) {
        auto* candidate_edge = candidate_edge_ref.get();
        if (candidate_edge == edge || !(*candidate_edge).isActive) { continue; }
        auto* candidate_vertex =
            (*candidate_edge).vT == pivot ? (*candidate_edge).vB.get() : (*candidate_edge).vT.get();
        if (candidate_vertex == vertex) { continue; }

        const auto cross = cross_product_sign((*vertex).pt, (*pivot).pt, (*candidate_vertex).pt);
        if (cross == 0) {
            if (((*vertex).pt.x > (*pivot).pt.x) == ((*pivot).pt.x > (*candidate_vertex).pt.x)) {
                continue;
            }
        } else if (cross > 0 ||
                   (alternate_vertex &&
                    !LeftTurning((*candidate_vertex).pt, (*pivot).pt, (*alternate_vertex).pt))) {
            continue;
        }
        alternate_vertex = candidate_vertex;
        alternate_edge = candidate_edge;
    }

    if (!alternate_vertex || (*alternate_vertex).pt.y < minimum_y) { return; }
    if ((*alternate_vertex).pt.y < (*pivot).pt.y) {
        if (is_left_edge(*alternate_edge)) { return; }
    } else if ((*alternate_vertex).pt.y > (*pivot).pt.y) {
        if (is_right_edge(*alternate_edge)) { return; }
    }

    auto* linking_edge =
        find_linking_edge(alternate_vertex, vertex, (*alternate_vertex).pt.y < (*vertex).pt.y);
    if (!linking_edge) {
        if ((*alternate_vertex).pt.y == (*vertex).pt.y && (*vertex).pt.y == minimum_y &&
            horizontal_between(context, alternate_vertex, vertex)) {
            return;
        }
        linking_edge = create_sweep_loose_edge(context, alternate_vertex, vertex);
    }

    if (!create_sweep_triangle(context, edge, alternate_edge, linking_edge)) { return; }
    if (!edge_completed(linking_edge)) {
        triangulate_left(context, linking_edge, alternate_vertex, minimum_y);
    }
}

auto triangulate_right(triangulation_context& context,
                       triangulation_edge* edge,
                       triangulation_vertex* pivot,
                       int64_t minimum_y) -> void {
    if (context.internal_error) { return; }
    triangulation_vertex* alternate_vertex = nullptr;
    triangulation_edge* alternate_edge = nullptr;
    auto* vertex = ((*edge).vB == pivot) ? (*edge).vT.get() : (*edge).vB.get();

    for (auto candidate_edge_ref : (*pivot).edges) {
        auto* candidate_edge = candidate_edge_ref.get();
        if (candidate_edge == edge || !(*candidate_edge).isActive) { continue; }
        auto* candidate_vertex =
            (*candidate_edge).vT == pivot ? (*candidate_edge).vB.get() : (*candidate_edge).vT.get();
        if (candidate_vertex == vertex) { continue; }

        const auto cross = cross_product_sign((*vertex).pt, (*pivot).pt, (*candidate_vertex).pt);
        if (cross == 0) {
            if (((*vertex).pt.x > (*pivot).pt.x) == ((*pivot).pt.x > (*candidate_vertex).pt.x)) {
                continue;
            }
        } else if (cross < 0 ||
                   (alternate_vertex &&
                    !RightTurning((*candidate_vertex).pt, (*pivot).pt, (*alternate_vertex).pt))) {
            continue;
        }
        alternate_vertex = candidate_vertex;
        alternate_edge = candidate_edge;
    }

    if (!alternate_vertex || (*alternate_vertex).pt.y < minimum_y) { return; }
    if ((*alternate_vertex).pt.y < (*pivot).pt.y) {
        if (is_right_edge(*alternate_edge)) { return; }
    } else if ((*alternate_vertex).pt.y > (*pivot).pt.y) {
        if (is_left_edge(*alternate_edge)) { return; }
    }

    auto* linking_edge =
        find_linking_edge(alternate_vertex, vertex, (*alternate_vertex).pt.y > (*vertex).pt.y);
    if (!linking_edge) {
        if ((*alternate_vertex).pt.y == (*vertex).pt.y && (*vertex).pt.y == minimum_y &&
            horizontal_between(context, alternate_vertex, vertex)) {
            return;
        }
        linking_edge = create_sweep_loose_edge(context, alternate_vertex, vertex);
    }

    if (!create_sweep_triangle(context, edge, linking_edge, alternate_edge)) { return; }
    if (!edge_completed(linking_edge)) {
        triangulate_right(context, linking_edge, alternate_vertex, minimum_y);
    }
}

}  // namespace clipper2next::internal
