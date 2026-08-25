#pragma once

#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_geometry.h"
#include "clip/engine/private/engine_horizontal.h"
#include "clip/engine/private/engine_output_topology.h"
#include "clip/engine/private/engine_scanline.h"
#include "clip/engine/private/engine_state.h"
#include "clip/engine/private/engine_winding.h"

namespace clipper2next::internal {

auto insert_left_edge(clipper_base_state& state, active_edge_node& edge) -> void;
auto adjust_curr_x_and_copy_to_sel(clipper_base_state& state, int64_t top_y) -> void;

inline auto initialize_bound_from_minimum(active_edge_node& bound,
                                          local_minimum_node& local_minima,
                                          Vertex* top_vertex,
                                          int wind_delta) -> void {
    bound.bottom = local_minima.vertex.get().pt;
    bound.current_x = bound.bottom.x;
    bound.wind_dx = wind_delta;
    bound.vertex_top = top_vertex;
    bound.top_point = bound.vertex_top->pt;
    bound.local_min = &local_minima;
    bound.dx = get_dx(bound.bottom, bound.top_point);
}

template <class AddLocalMinPoly, class CheckJoinLeft, class CheckJoinRight, class IntersectEdges>
auto insert_local_minima_into_ael(clipper_base_state& state,
                                  int64_t bot_y,
                                  AddLocalMinPoly add_local_min_poly,
                                  CheckJoinLeft check_join_left,
                                  CheckJoinRight check_join_right,
                                  IntersectEdges intersect_edges) -> void {
    local_minimum_node* local_minima = nullptr;
    active_edge_node* left_bound = nullptr;
    active_edge_node* right_bound = nullptr;

    while (pop_local_minima(
        state.current_locmin_iter_, state.minima_list_.end(), bot_y, local_minima)) {
        auto& minimum_vertex = local_minima->vertex.get();
        if ((minimum_vertex.flags & VertexFlags::OpenStart) != VertexFlags::Empty) {
            left_bound = nullptr;
        } else {
            left_bound = &state.active_pool_.emplace();
            initialize_bound_from_minimum(*left_bound, *local_minima, minimum_vertex.prev, -1);
        }

        if ((minimum_vertex.flags & VertexFlags::OpenEnd) != VertexFlags::Empty) {
            right_bound = nullptr;
        } else {
            right_bound = &state.active_pool_.emplace();
            initialize_bound_from_minimum(*right_bound, *local_minima, minimum_vertex.next, 1);
        }

        if (left_bound && right_bound) {
            if (is_horizontal(*left_bound)) {
                if (is_heading_right_horizontal(*left_bound)) {
                    swap_actives(left_bound, right_bound);
                }
            } else if (is_horizontal(*right_bound)) {
                if (is_heading_left_horizontal(*right_bound)) {
                    swap_actives(left_bound, right_bound);
                }
            } else if (left_bound->dx < right_bound->dx) {
                swap_actives(left_bound, right_bound);
            }
        } else if (!left_bound) {
            left_bound = right_bound;
            right_bound = nullptr;
        }

        bool contributing = false;
        left_bound->is_left_bound = true;
        insert_left_edge(state, *left_bound);

        if (is_open(*left_bound)) {
            set_wind_count_for_open_path_edge(state, *left_bound);
            contributing = is_contributing_open_edge(state.cliptype_, state.fillrule_, *left_bound);
        } else {
            set_wind_count_for_closed_path_edge(state, *left_bound);
            contributing =
                is_contributing_closed_edge(state.cliptype_, state.fillrule_, *left_bound);
        }

        if (right_bound) {
            right_bound->is_left_bound = false;
            right_bound->winding_count = left_bound->winding_count;
            right_bound->wind_cnt2 = left_bound->wind_cnt2;
            insert_right_edge(*left_bound, *right_bound);
            if (contributing) {
                add_local_min_poly(*left_bound, *right_bound, left_bound->bottom, true);
                if (!is_horizontal(*left_bound)) {
                    check_join_left(*left_bound, left_bound->bottom, false);
                }
            }

            while (right_bound->next_in_ael &&
                   is_valid_ael_order(*right_bound->next_in_ael, *right_bound)) {
                intersect_edges(*right_bound, *right_bound->next_in_ael, right_bound->bottom);
                swap_positions_in_ael(*right_bound, *right_bound->next_in_ael, state.actives_);
            }

            if (is_horizontal(*right_bound)) {
                push_horizontal(state.sel_, *right_bound);
            } else {
                check_join_right(*right_bound, right_bound->bottom, false);
                push_scanline(state.scanline_list_, right_bound->top_point.y);
            }
        } else if (contributing) {
            start_open_path(state.output_owner_, *left_bound, left_bound->bottom);
        }

        if (is_horizontal(*left_bound)) {
            push_horizontal(state.sel_, *left_bound);
        } else {
            push_scanline(state.scanline_list_, left_bound->top_point.y);
        }
    }
}

template <class SplitJoined, class CheckJoinLeft, class CheckJoinRight>
auto update_edge_into_ael(clipper_base_state& state,
                          active_edge_node& edge,
                          bool preserve_collinear,
                          SplitJoined split_joined,
                          CheckJoinLeft check_join_left,
                          CheckJoinRight check_join_right) -> void {
    edge.bottom = edge.top_point;
    edge.vertex_top = next_vertex(edge);
    edge.top_point = edge.vertex_top->pt;
    edge.current_x = edge.bottom.x;
    edge.dx = get_dx(edge.bottom, edge.top_point);

    if (edge.join_with != JoinWith::NoJoin) { split_joined(edge, edge.bottom); }

    if (is_horizontal(edge)) {
        if (!is_open(edge)) { trim_horizontal(edge, preserve_collinear); }
        return;
    }

    push_scanline(state.scanline_list_, edge.top_point.y);
    check_join_left(edge, edge.bottom);
    check_join_right(edge, edge.bottom, true);
}

}  // namespace clipper2next::internal
