#include "clip/engine/private/engine_scanbeam_orchestrator_helpers.h"

#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_ael_builder.h"
#include "clip/engine/private/engine_output_topology.h"
#include "clip/engine/private/engine_winding.h"

namespace clipper2next::internal {

auto is_joined(const active_edge_node& edge) noexcept -> bool {
    return edge.join_with != JoinWith::NoJoin;
}

auto current_y_maxima_vertex_open(const active_edge_node& edge) -> Vertex* {
    Vertex* result = edge.vertex_top;
    if (edge.wind_dx > 0) {
        while ((result->next->pt.y == result->pt.y) &&
               ((result->flags & (VertexFlags::OpenEnd | VertexFlags::LocalMax)) ==
                VertexFlags::Empty)) {
            result = result->next;
        }
    } else {
        while ((result->prev->pt.y == result->pt.y) &&
               ((result->flags & (VertexFlags::OpenEnd | VertexFlags::LocalMax)) ==
                VertexFlags::Empty)) {
            result = result->prev;
        }
    }
    if (!active_is_maxima(*result)) { result = nullptr; }
    return result;
}

auto current_y_maxima_vertex(const active_edge_node& edge) -> Vertex* {
    Vertex* result = edge.vertex_top;
    if (edge.wind_dx > 0) {
        while (result->next->pt.y == result->pt.y) { result = result->next; }
    } else {
        while (result->prev->pt.y == result->pt.y) { result = result->prev; }
    }
    if (!active_is_maxima(*result)) { result = nullptr; }
    return result;
}

auto maxima_pair(const active_edge_node& edge) noexcept -> active_edge_node* {
    active_edge_node* result = edge.next_in_ael;
    while (result) {
        if (result->vertex_top == edge.vertex_top) { return result; }
        result = result->next_in_ael;
    }
    return nullptr;
}

auto last_output_point(const active_edge_node& hot_edge) noexcept -> output_point_node* {
    output_record_node* output_record = hot_edge.outrec;
    output_point_node* result = output_record->pts;
    if (&hot_edge != output_record->front_edge) { result = result->next; }
    return result;
}

auto update_scanbeam_edge(clipper_base_state& state,
                          active_edge_node& edge,
                          bool preserve_collinear,
                          bool& succeeded) -> void {
    update_edge_into_ael(
        state,
        edge,
        preserve_collinear,
        [&state](active_edge_node& joined, const Point64& point) {
            split_joined_edge(state, joined, point);
        },
        [&state, &succeeded](active_edge_node& joined, const Point64& point) {
            check_join_left(state, succeeded, joined, point);
        },
        [&state, &succeeded](active_edge_node& joined, const Point64& point, bool check_curr_x) {
            check_join_right(state, succeeded, joined, point, check_curr_x);
        });
}

auto add_local_max_poly(clipper_base_state& state,
                        active_edge_node& first,
                        active_edge_node& second,
                        const Point64& point,
                        bool& succeeded) -> output_point_node* {
    auto result = add_local_max_polygon(
        state, first, second, point, [&state](active_edge_node& edge, const Point64& split_point) {
            split_joined_edge(state, edge, split_point);
        });
    if (!result.succeeded) { succeeded = false; }
    return result.output_point ? &result.output_point->get() : nullptr;
}

}  // namespace clipper2next::internal
