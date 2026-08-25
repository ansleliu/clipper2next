#include "clip/engine/private/engine_scanbeam_orchestrator.h"

#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_horizontal.h"
#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_output_topology.h"
#include "clip/engine/private/engine_scanbeam_orchestrator_helpers.h"
#include "clip/engine/private/engine_winding.h"

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto horizontal_range_exceeded(const active_edge_node& edge,
                                             bool is_left_to_right,
                                             int64_t horizontal_left,
                                             int64_t horizontal_right) -> bool {
    return (is_left_to_right && edge.current_x > horizontal_right) ||
           (!is_left_to_right && edge.current_x < horizontal_left);
}

[[nodiscard]] auto should_stop_at_top_x(const active_edge_node& horizontal,
                                        const active_edge_node& edge,
                                        bool is_left_to_right,
                                        const Point64& point) -> bool {
    const auto edge_top_x = top_x(edge, point.y);
    const auto is_cold_open_crossing =
        is_open(edge) && !is_same_poly_type(edge, horizontal) && !is_hot_edge(edge);

    if (is_left_to_right) {
        return is_cold_open_crossing ? edge_top_x > point.x : edge_top_x >= point.x;
    }
    return is_cold_open_crossing ? edge_top_x < point.x : edge_top_x <= point.x;
}

[[nodiscard]] auto should_stop_before_horizontal_intersection(const active_edge_node& horizontal,
                                                              const active_edge_node& edge,
                                                              const Vertex* max_vertex,
                                                              bool is_left_to_right,
                                                              int64_t horizontal_left,
                                                              int64_t horizontal_right) -> bool {
    if (max_vertex == horizontal.vertex_top && !is_open_end(horizontal)) { return false; }
    if (horizontal_range_exceeded(edge, is_left_to_right, horizontal_left, horizontal_right)) {
        return true;
    }
    if (edge.current_x != horizontal.top_point.x || is_horizontal(edge)) { return false; }
    return should_stop_at_top_x(horizontal, edge, is_left_to_right, next_vertex(horizontal)->pt);
}

auto close_open_horizontal(clipper_base_state& state, active_edge_node& horizontal) -> void {
    if (is_hot_edge(horizontal)) {
        add_output_point(horizontal, horizontal.top_point);
        if (is_front(horizontal)) {
            horizontal.outrec->front_edge = nullptr;
        } else {
            horizontal.outrec->back_edge = nullptr;
        }
        horizontal.outrec = nullptr;
    }
    remove_from_ael(horizontal, state.actives_);
}

}  // namespace

auto do_horizontal(clipper_base_state& state,
                   active_edge_node& horizontal,
                   const engine_scanbeam_orchestration_options& options,
                   bool& succeeded) -> void {
    Point64 point;
    const bool horizontal_is_open = is_open(horizontal);
    const int64_t y = horizontal.bottom.y;
    Vertex* max_vertex = horizontal_is_open ? current_y_maxima_vertex_open(horizontal)
                                            : current_y_maxima_vertex(horizontal);

    int64_t horizontal_left = 0;
    int64_t horizontal_right = 0;
    bool is_left_to_right =
        reset_horizontal_direction(horizontal, max_vertex, horizontal_left, horizontal_right);

    if (is_hot_edge(horizontal)) {
        output_point_node* output_point = add_output_point(
            horizontal, Point64(horizontal.current_x, y));
        add_trial_horizontal_join(state.horz_seg_list_, output_point);
    }

    while (true) {
        active_edge_node* edge =
            is_left_to_right ? horizontal.next_in_ael.get() : horizontal.prev_in_ael.get();

        while (edge) {
            if (edge->vertex_top == max_vertex) {
                if (is_hot_edge(horizontal) && is_joined(*edge)) {
                    split_joined_edge(state, *edge, edge->top_point);
                }

                if (is_hot_edge(horizontal)) {
                    while (horizontal.vertex_top != max_vertex) {
                        add_output_point(horizontal, horizontal.top_point);
                        update_scanbeam_edge(
                            state, horizontal, options.preserve_collinear, succeeded);
                    }
                    if (is_left_to_right) {
                        add_local_max_poly(
                            state, horizontal, *edge, horizontal.top_point, succeeded);
                    } else {
                        add_local_max_poly(
                            state, *edge, horizontal, horizontal.top_point, succeeded);
                    }
                }
                remove_from_ael(*edge, state.actives_);
                remove_from_ael(horizontal, state.actives_);
                return;
            }

            if (should_stop_before_horizontal_intersection(horizontal,
                                                           *edge,
                                                           max_vertex,
                                                           is_left_to_right,
                                                           horizontal_left,
                                                           horizontal_right)) {
                break;
            }

            point = Point64(edge->current_x, horizontal.bottom.y);
            if (is_left_to_right) {
                intersect_edges(state,
                                options.has_open_paths,
                                succeeded,
                                horizontal,
                                *edge,
                                point);
                swap_positions_in_ael(horizontal, *edge, state.actives_);
                check_join_left(state, succeeded, *edge, point);
                horizontal.current_x = edge->current_x;
                edge = horizontal.next_in_ael;
            } else {
                intersect_edges(state,
                                options.has_open_paths,
                                succeeded,
                                *edge,
                                horizontal,
                                point);
                swap_positions_in_ael(*edge, horizontal, state.actives_);
                check_join_right(state, succeeded, *edge, point);
                horizontal.current_x = edge->current_x;
                edge = horizontal.prev_in_ael;
            }

            if (horizontal.outrec) {
                add_trial_horizontal_join(state.horz_seg_list_, last_output_point(horizontal));
            }
        }

        if (horizontal_is_open && is_open_end(horizontal)) {
            close_open_horizontal(state, horizontal);
            return;
        }
        if (next_vertex(horizontal)->pt.y != horizontal.top_point.y) { break; }

        if (is_hot_edge(horizontal)) { add_output_point(horizontal, horizontal.top_point); }
        update_scanbeam_edge(state, horizontal, options.preserve_collinear, succeeded);

        is_left_to_right =
            reset_horizontal_direction(horizontal, max_vertex, horizontal_left, horizontal_right);
    }

    if (is_hot_edge(horizontal)) {
        output_point_node* output_point = add_output_point(horizontal, horizontal.top_point);
        add_trial_horizontal_join(state.horz_seg_list_, output_point);
    }

    update_scanbeam_edge(state, horizontal, options.preserve_collinear, succeeded);
}

}  // namespace clipper2next::internal
