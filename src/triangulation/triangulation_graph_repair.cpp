#include "triangulation/private/triangulation_graph_repair.h"

#include "triangulation/private/triangulation_intersections.h"
#include "triangulation/private/triangulation_topology.h"

#include <algorithm>
#include <cmath>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto local_minimum_angle(triangulation_vertex* vertex) -> double {
    int ascending = 0;
    int descending = 1;
    if ((*(*vertex).edges[0]).kind != triangulation_edge_kind::ascend) {
        descending = 0;
        ascending = 1;
    }

    const auto* descending_edge = (*vertex).edges[descending].get();
    const auto* ascending_edge = (*vertex).edges[ascending].get();
    const auto& from = (*(*descending_edge).vT).pt;
    const auto& pivot = (*vertex).pt;
    const auto& to = (*(*ascending_edge).vT).pt;
    const auto abx = static_cast<double>(pivot.x - from.x);
    const auto aby = static_cast<double>(pivot.y - from.y);
    const auto bcx = static_cast<double>(pivot.x - to.x);
    const auto bcy = static_cast<double>(pivot.y - to.y);
    const auto dot_product = abx * bcx + aby * bcy;
    const auto cross_product = abx * bcy - aby * bcx;
    return std::atan2(cross_product, dot_product);
}

auto split_triangulation_edge(triangulation_context& context,
                              triangulation_edge* long_edge,
                              triangulation_edge* short_edge) -> void {
    auto* old_top = (*long_edge).vT.get();
    auto* new_top = (*short_edge).vT.get();
    if (!remove_edge_from_vertex(context, old_top, long_edge)) { return; }

    (*long_edge).vT = new_top;
    if ((*long_edge).vL == old_top) {
        (*long_edge).vL = new_top;
    } else {
        (*long_edge).vR = new_top;
    }
    (*new_top).edges.push_back(long_edge);
    (void)create_triangulation_edge(context, new_top, old_top, (*long_edge).kind);
}

[[nodiscard]] auto remove_intersection(triangulation_context& context,
                                       triangulation_edge* first,
                                       triangulation_edge* second) -> bool {
    auto* vertex = (*first).vL.get();
    auto* split_edge = second;
    auto distance =
        ShortestDistFromSegment((*(*first).vL).pt, (*(*second).vL).pt, (*(*second).vR).pt);

    auto candidate_distance =
        ShortestDistFromSegment((*(*first).vR).pt, (*(*second).vL).pt, (*(*second).vR).pt);
    if (candidate_distance < distance) {
        distance = candidate_distance;
        vertex = (*first).vR.get();
    }

    candidate_distance =
        ShortestDistFromSegment((*(*second).vL).pt, (*(*first).vL).pt, (*(*first).vR).pt);
    if (candidate_distance < distance) {
        distance = candidate_distance;
        split_edge = first;
        vertex = (*second).vL.get();
    }

    candidate_distance =
        ShortestDistFromSegment((*(*second).vR).pt, (*(*first).vL).pt, (*(*first).vR).pt);
    if (candidate_distance < distance) {
        distance = candidate_distance;
        split_edge = first;
        vertex = (*second).vR.get();
    }

    if (distance > 1.000) { return false; }

    auto* old_top = (*split_edge).vT.get();
    if (!remove_edge_from_vertex(context, old_top, split_edge)) { return false; }
    if ((*split_edge).vL == old_top) {
        (*split_edge).vL = vertex;
    } else {
        (*split_edge).vR = vertex;
    }
    (*split_edge).vT = vertex;
    (*vertex).edges.push_back(split_edge);
    (*vertex).innerLM = false;
    if ((*(*split_edge).vB).innerLM && local_minimum_angle((*split_edge).vB) <= 0) {
        (*(*split_edge).vB).innerLM = false;
    }
    (void)create_triangulation_edge(context, vertex, old_top, (*split_edge).kind);
    return true;
}

}  // namespace

auto repair_triangulation_graph(triangulation_context& context) -> bool {
    for (std::size_t first_index = 0; first_index < context.edges.size(); ++first_index) {
        auto* first = context.edges[first_index].get();
        for (auto second_index = first_index + 1; second_index < context.edges.size();
             ++second_index) {
            auto* second = context.edges[second_index].get();
            if ((*(*second).vL).pt.x >= (*(*first).vR).pt.x) { break; }
            if ((*(*second).vT).pt.y < (*(*first).vB).pt.y &&
                (*(*second).vB).pt.y > (*(*first).vT).pt.y &&
                SegsIntersect(
                    (*(*second).vL).pt, (*(*second).vR).pt, (*(*first).vL).pt, (*(*first).vR).pt) ==
                    triangulation_intersect_kind::intersect) {
                if (!remove_intersection(context, second, first)) { return false; }
            }
        }
    }
    return true;
}

auto merge_duplicate_or_collinear_vertices(triangulation_context& context) -> void {
    if (context.vertices.size() < 2) { return; }

    auto first_vertex_iterator = context.vertices.begin();
    for (auto second_vertex_iterator = context.vertices.begin() + 1;
         second_vertex_iterator != context.vertices.end();
         ++second_vertex_iterator) {
        if ((*first_vertex_iterator)->pt != (*second_vertex_iterator)->pt) {
            first_vertex_iterator = second_vertex_iterator;
            continue;
        }

        auto* first_vertex = (*first_vertex_iterator).get();
        auto* second_vertex = (*second_vertex_iterator).get();
        if (!(*first_vertex).innerLM || !(*second_vertex).innerLM) {
            (*first_vertex).innerLM = false;
        }

        for (auto edge_ref : (*second_vertex).edges) {
            auto* edge = edge_ref.get();
            if ((*edge).vB == second_vertex) {
                (*edge).vB = first_vertex;
            } else {
                (*edge).vT = first_vertex;
            }
            if ((*edge).vL == second_vertex) {
                (*edge).vL = first_vertex;
            } else {
                (*edge).vR = first_vertex;
            }
        }
        std::copy((*second_vertex).edges.begin(),
                  (*second_vertex).edges.end(),
                  std::back_inserter((*first_vertex).edges));
        (*second_vertex).edges.clear();

        for (auto edge_iterator = (*first_vertex).edges.begin();
             edge_iterator != (*first_vertex).edges.end();
             ++edge_iterator) {
            if (is_horizontal_edge(**edge_iterator) || (*edge_iterator)->vB != first_vertex) {
                continue;
            }
            for (auto second_edge_iterator = edge_iterator + 1;
                 second_edge_iterator != (*first_vertex).edges.end();
                 ++second_edge_iterator) {
                auto* first_edge = (*edge_iterator).get();
                auto* second_edge = (*second_edge_iterator).get();
                if ((*second_edge).vB != first_vertex ||
                    (*(*first_edge).vT).pt.y == (*(*second_edge).vT).pt.y ||
                    cross_product_sign(
                        (*(*first_edge).vT).pt, (*first_vertex).pt, (*(*second_edge).vT).pt) != 0) {
                    continue;
                }
                if ((*(*first_edge).vT).pt.y < (*(*second_edge).vT).pt.y) {
                    split_triangulation_edge(context, first_edge, second_edge);
                } else {
                    split_triangulation_edge(context, second_edge, first_edge);
                }
                if (context.internal_error) { return; }
                break;
            }
        }
    }
}

}  // namespace clipper2next::internal
