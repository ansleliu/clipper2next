#include "clip/engine/private/engine_output_topology.h"

#include "clip/engine/private/engine_geometry.h"
#include "clip/engine/private/engine_winding.h"
#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {

auto get_previous_hot_edge(const active_edge_node& edge) noexcept -> active_edge_node* {
    active_edge_node* previous = edge.prev_in_ael;
    while (previous && (is_open(*previous) || !is_hot_edge(*previous))) {
        previous = previous->prev_in_ael;
    }
    return previous;
}

auto add_local_min_polygon(clipper_base_state& state,
                           active_edge_node& first,
                           active_edge_node& second,
                           const Point64& point,
                           bool is_new) -> output_point_node* {
    output_record_node* output_record = &state.output_owner_.create_outrec();
    first.outrec = output_record;
    second.outrec = output_record;

    if (is_open(first)) {
        output_record->owner = nullptr;
        output_record->is_open = true;
        if (first.wind_dx > 0) {
            set_sides(*output_record, first, second);
        } else {
            set_sides(*output_record, second, first);
        }
    } else {
        active_edge_node* previous_hot_edge = get_previous_hot_edge(first);
        if (previous_hot_edge) {
            if (state.using_polytree_) { set_owner(output_record, previous_hot_edge->outrec); }
            if (is_front(*previous_hot_edge) == is_new) {
                set_sides(*output_record, second, first);
            } else {
                set_sides(*output_record, first, second);
            }
        } else {
            output_record->owner = nullptr;
            if (is_new) {
                set_sides(*output_record, first, second);
            } else {
                set_sides(*output_record, second, first);
            }
        }
    }

    output_point_node* output_point = &state.output_owner_.create_outpt(point, *output_record);
    output_record->pts = output_point;
    return output_point;
}

auto split_joined_edge(clipper_base_state& state, active_edge_node& edge, const Point64& point)
    -> void {
    if (edge.join_with == JoinWith::Right) {
        edge.join_with = JoinWith::NoJoin;
        edge.next_in_ael->join_with = JoinWith::NoJoin;
        add_local_min_polygon(state, edge, *edge.next_in_ael, point, true);
    } else {
        edge.join_with = JoinWith::NoJoin;
        edge.prev_in_ael->join_with = JoinWith::NoJoin;
        add_local_min_polygon(state, *edge.prev_in_ael, edge, point, true);
    }
}

auto check_join_left(clipper_base_state& state,
                     bool& succeeded,
                     active_edge_node& edge,
                     const Point64& point,
                     bool check_curr_x) -> void {
    active_edge_node* previous = edge.prev_in_ael;
    if (!previous || !is_hot_edge(edge) || !is_hot_edge(*previous) || is_horizontal(edge) ||
        is_horizontal(*previous) || is_open(edge) || is_open(*previous)) {
        return;
    }
    if ((point.y < edge.top_point.y + 2 || point.y < previous->top_point.y + 2) &&
        ((edge.bottom.y > point.y) || (previous->bottom.y > point.y))) {
        return;
    }

    if (check_curr_x) {
        if (perpendicular_distance_from_line_squared(point, previous->bottom, previous->top_point) >
            0.25) {
            return;
        }
    } else if (edge.current_x != previous->current_x) {
        return;
    }
    if (cross_product_sign_in_clipper_range(edge.top_point, point, previous->top_point) != 0) {
        return;
    }

    if (edge.outrec->idx == previous->outrec->idx) {
        auto result =
            add_local_max_polygon(state,
                                  *previous,
                                  edge,
                                  point,
                                  [&](active_edge_node& joined, const Point64& split_point) {
                                      split_joined_edge(state, joined, split_point);
                                  });
        if (!result.succeeded) { succeeded = false; }
    } else if (edge.outrec->idx < previous->outrec->idx) {
        join_outrec_paths(edge, *previous);
    } else {
        join_outrec_paths(*previous, edge);
    }
    previous->join_with = JoinWith::Right;
    edge.join_with = JoinWith::Left;
}

auto check_join_right(clipper_base_state& state,
                      bool& succeeded,
                      active_edge_node& edge,
                      const Point64& point,
                      bool check_curr_x) -> void {
    active_edge_node* next = edge.next_in_ael;
    if (!next || !is_hot_edge(edge) || !is_hot_edge(*next) || is_horizontal(edge) ||
        is_horizontal(*next) || is_open(edge) || is_open(*next)) {
        return;
    }
    if ((point.y < edge.top_point.y + 2 || point.y < next->top_point.y + 2) &&
        ((edge.bottom.y > point.y) || (next->bottom.y > point.y))) {
        return;
    }

    if (check_curr_x) {
        if (perpendicular_distance_from_line_squared(point, next->bottom, next->top_point) > 0.35) {
            return;
        }
    } else if (edge.current_x != next->current_x) {
        return;
    }
    if (cross_product_sign_in_clipper_range(edge.top_point, point, next->top_point) != 0) {
        return;
    }

    if (edge.outrec->idx == next->outrec->idx) {
        auto result = add_local_max_polygon(
            state, edge, *next, point, [&](active_edge_node& joined, const Point64& split_point) {
                split_joined_edge(state, joined, split_point);
            });
        if (!result.succeeded) { succeeded = false; }
    } else if (edge.outrec->idx < next->outrec->idx) {
        join_outrec_paths(edge, *next);
    } else {
        join_outrec_paths(*next, edge);
    }

    edge.join_with = JoinWith::Right;
    next->join_with = JoinWith::Left;
}

}  // namespace clipper2next::internal
