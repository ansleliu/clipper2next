#pragma once

#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_output_owner.h"
#include "clip/engine/private/engine_state.h"
#include "clip/engine/private/engine_topology.h"
#include "clip/engine/private/engine_winding.h"

#include <functional>
#include <optional>

namespace clipper2next::internal {

struct local_max_polygon_result {
    std::optional<std::reference_wrapper<output_point_node>> output_point;
    bool succeeded = true;
};

[[nodiscard]] inline auto insert_output_point_after(output_point_node* previous,
                                                    const Point64& point) -> output_point_node* {
    auto* result = duplicate_out_point(previous, true);
    result->pt = point;
    return result;
}

inline auto add_output_point(const active_edge_node& edge, const Point64& point)
    -> output_point_node* {
    auto* output_record = edge.outrec.get();
    const bool to_front = is_front(edge);
    auto* front = output_record->pts.get();
    auto* back = front->next.get();

    if (to_front) {
        if (point == front->pt) { return front; }
    } else if (point == back->pt) {
        return back;
    }

    auto* result = insert_output_point_after(front, point);
    if (to_front) { output_record->pts = result; }
    return result;
}

auto join_outrec_paths(active_edge_node& first, active_edge_node& second) -> void;
[[nodiscard]] auto get_previous_hot_edge(const active_edge_node& edge) noexcept
    -> active_edge_node*;

auto add_local_min_polygon(clipper_base_state& state,
                           active_edge_node& first,
                           active_edge_node& second,
                           const Point64& point,
                           bool is_new = false) -> output_point_node*;

auto split_joined_edge(clipper_base_state& state, active_edge_node& edge, const Point64& point)
    -> void;

auto check_join_left(clipper_base_state& state,
                     bool& succeeded,
                     active_edge_node& edge,
                     const Point64& point,
                     bool check_curr_x = false) -> void;

auto check_join_right(clipper_base_state& state,
                      bool& succeeded,
                      active_edge_node& edge,
                      const Point64& point,
                      bool check_curr_x = false) -> void;

template <class SplitJoined>
auto add_local_max_polygon(clipper_base_state& state,
                           active_edge_node& first,
                           active_edge_node& second,
                           const Point64& point,
                           SplitJoined split_joined) -> local_max_polygon_result {
    if (first.join_with != JoinWith::NoJoin) { split_joined(first, point); }
    if (second.join_with != JoinWith::NoJoin) { split_joined(second, point); }

    if (is_front(first) == is_front(second)) {
        if (is_open_end(first)) {
            swap_sides(*first.outrec);
        } else if (is_open_end(second)) {
            swap_sides(*second.outrec);
        } else {
            return {std::nullopt, false};
        }
    }

    output_point_node* result = add_output_point(first, point);
    if (first.outrec == second.outrec) {
        output_record_node& output_record = *first.outrec;
        output_record.pts = result;

        if (state.using_polytree_) {
            active_edge_node* previous_hot_edge = get_previous_hot_edge(first);
            if (!previous_hot_edge) {
                output_record.owner = nullptr;
            } else {
                set_owner(&output_record, previous_hot_edge->outrec);
            }
        }

        uncouple_outrec(first);
        result = output_record.pts.get();
        if (output_record.owner && !output_record.owner->front_edge) {
            output_record.owner = get_real_outrec(output_record.owner);
        }
    } else if (is_open(first)) {
        if (first.wind_dx < 0) {
            join_outrec_paths(first, second);
        } else {
            join_outrec_paths(second, first);
        }
    } else if (first.outrec->idx < second.outrec->idx) {
        join_outrec_paths(first, second);
    } else {
        join_outrec_paths(second, first);
    }
    return {std::ref(*result), true};
}

inline auto start_open_path(engine_output_owner& output_owner,
                            active_edge_node& edge,
                            const Point64& point) -> output_point_node* {
    auto* output_record = &output_owner.create_outrec();
    output_record->is_open = true;

    if (edge.wind_dx > 0) {
        output_record->front_edge = &edge;
        output_record->back_edge = nullptr;
    } else {
        output_record->front_edge = nullptr;
        output_record->back_edge = &edge;
    }

    edge.outrec = output_record;

    auto* output_point = &output_owner.create_outpt(point, *output_record);
    output_record->pts = output_point;
    return output_point;
}

}  // namespace clipper2next::internal
