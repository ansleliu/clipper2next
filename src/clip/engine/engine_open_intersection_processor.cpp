#include "clip/engine/private/engine_intersection_cases.h"

#include "clip/engine/private/engine_horizontal.h"
#include "clip/engine/private/engine_output_topology.h"
#include "clip/engine/private/engine_topology.h"
#include "clip/engine/private/engine_winding.h"

#include <cstdlib>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto is_joined(const active_edge_node& edge) noexcept -> bool {
    return edge.join_with != JoinWith::NoJoin;
}

[[nodiscard]] auto find_edge_with_matching_local_minimum(active_edge_node* edge)
    -> active_edge_node* {
    active_edge_node* result = edge->next_in_ael;
    while (result) {
        if (result->local_min == edge->local_min) { return result; }
        if (!is_horizontal(*result) && edge->bottom != result->bottom) {
            result = nullptr;
        } else {
            result = result->next_in_ael;
        }
    }

    result = edge->prev_in_ael;
    while (result) {
        if (result->local_min == edge->local_min) { return result; }
        if (!is_horizontal(*result) && edge->bottom != result->bottom) { return nullptr; }
        result = result->prev_in_ael;
    }
    return result;
}

auto attach_open_edge_to_matching_path(active_edge_node& open_edge, active_edge_node& matching_edge)
    -> void {
    open_edge.outrec = matching_edge.outrec;
    if (open_edge.wind_dx > 0) {
        set_sides(*matching_edge.outrec, open_edge, matching_edge);
    } else {
        set_sides(*matching_edge.outrec, matching_edge, open_edge);
    }
}

}  // namespace

auto try_intersect_open_edges(clipper_base_state& state,
                              bool has_open_paths,
                              active_edge_node& first,
                              active_edge_node& second,
                              const Point64& point) -> bool {
    if (!has_open_paths || (!is_open(first) && !is_open(second))) { return false; }
    if (is_open(first) && is_open(second)) { return true; }

    auto* open_edge = is_open(first) ? &first : &second;
    auto* closed_edge = is_open(first) ? &second : &first;

    if (is_joined(*closed_edge)) { split_joined_edge(state, *closed_edge, point); }

    if (std::abs(closed_edge->winding_count) != 1) { return true; }

    switch (state.cliptype_) {
    case ClipType::Union: {
        if (!is_hot_edge(*closed_edge)) { return true; }
        break;
    }
    default: {
        if (closed_edge->local_min->polytype == PathType::Subject) { return true; }
    }
    }

    switch (state.fillrule_) {
    case FillRule::Positive: {
        if (closed_edge->winding_count != 1) { return true; }
        break;
    }
    case FillRule::Negative: {
        if (closed_edge->winding_count != -1) { return true; }
        break;
    }
    default: {
        if (std::abs(closed_edge->winding_count) != 1) { return true; }
    }
    }

    if (is_hot_edge(*open_edge)) {
        static_cast<void>(add_output_point(*open_edge, point));
        if (is_front(*open_edge)) {
            open_edge->outrec->front_edge = nullptr;
        } else {
            open_edge->outrec->back_edge = nullptr;
        }
        open_edge->outrec = nullptr;
    } else if (point == open_edge->local_min->vertex.get().pt &&
               !is_open_end(open_edge->local_min->vertex.get())) {
        active_edge_node* matching_edge = find_edge_with_matching_local_minimum(open_edge);
        if (matching_edge && is_hot_edge(*matching_edge)) {
            attach_open_edge_to_matching_path(*open_edge, *matching_edge);
            return true;
        }
        static_cast<void>(start_open_path(state.output_owner_, *open_edge, point));
    } else {
        static_cast<void>(start_open_path(state.output_owner_, *open_edge, point));
    }

    return true;
}

}  // namespace clipper2next::internal
