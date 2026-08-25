#include "clip/engine/private/engine_winding.h"

#include <cstdlib>

namespace clipper2next::internal {

namespace {

auto is_odd(int value) noexcept -> bool {
    return (value & 1) != 0;
}

}  // namespace

auto set_wind_count_for_closed_path_edge(clipper_base_state& state, active_edge_node& edge)
    -> void {
    active_edge_node* previous = edge.prev_in_ael;
    const PathType polytype = get_poly_type(edge);
    while (previous && (get_poly_type(*previous) != polytype || is_open(*previous))) {
        previous = previous->prev_in_ael;
    }

    if (!previous) {
        edge.winding_count = edge.wind_dx;
        previous = state.actives_;
    } else if (state.fillrule_ == FillRule::EvenOdd) {
        edge.winding_count = edge.wind_dx;
        edge.wind_cnt2 = previous->wind_cnt2;
        previous = previous->next_in_ael;
    } else {
        if (previous->winding_count * previous->wind_dx < 0) {
            if (std::abs(previous->winding_count) > 1) {
                if (previous->wind_dx * edge.wind_dx < 0) {
                    edge.winding_count = previous->winding_count;
                } else {
                    edge.winding_count = previous->winding_count + edge.wind_dx;
                }
            } else {
                edge.winding_count = is_open(edge) ? 1 : edge.wind_dx;
            }
        } else {
            if (previous->wind_dx * edge.wind_dx < 0) {
                edge.winding_count = previous->winding_count;
            } else {
                edge.winding_count = previous->winding_count + edge.wind_dx;
            }
        }
        edge.wind_cnt2 = previous->wind_cnt2;
        previous = previous->next_in_ael;
    }

    if (state.fillrule_ == FillRule::EvenOdd) {
        while (previous != &edge) {
            if (get_poly_type(*previous) != polytype && !is_open(*previous)) {
                edge.wind_cnt2 = edge.wind_cnt2 == 0 ? 1 : 0;
            }
            previous = previous->next_in_ael;
        }
    } else {
        while (previous != &edge) {
            if (get_poly_type(*previous) != polytype && !is_open(*previous)) {
                edge.wind_cnt2 += previous->wind_dx;
            }
            previous = previous->next_in_ael;
        }
    }
}

auto set_wind_count_for_open_path_edge(const clipper_base_state& state, active_edge_node& edge)
    -> void {
    active_edge_node* current = state.actives_;
    if (state.fillrule_ == FillRule::EvenOdd) {
        int subject_count = 0;
        int clip_count = 0;
        while (current != &edge) {
            if (get_poly_type(*current) == PathType::Clip) {
                ++clip_count;
            } else if (!is_open(*current)) {
                ++subject_count;
            }
            current = current->next_in_ael;
        }
        edge.winding_count = is_odd(subject_count) ? 1 : 0;
        edge.wind_cnt2 = is_odd(clip_count) ? 1 : 0;
    } else {
        while (current != &edge) {
            if (get_poly_type(*current) == PathType::Clip) {
                edge.wind_cnt2 += current->wind_dx;
            } else if (!is_open(*current)) {
                edge.winding_count += current->wind_dx;
            }
            current = current->next_in_ael;
        }
    }
}

}  // namespace clipper2next::internal
