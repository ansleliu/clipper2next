#include "clip/engine/private/engine_scanbeam_orchestrator.h"

#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_horizontal.h"
#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_output_topology.h"
#include "clip/engine/private/engine_scanbeam_orchestrator_helpers.h"
#include "clip/engine/private/engine_winding.h"

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto active_edge_is_linked(const clipper_base_state& state,
                                         const active_edge_node& edge) noexcept -> bool {
    return state.actives_ == &edge || edge.prev_in_ael != nullptr || edge.next_in_ael != nullptr;
}

auto do_top_edge(clipper_base_state& state,
                 active_edge_node& edge,
                 int64_t top_y,
                  const engine_scanbeam_orchestration_options& options,
                  bool& succeeded) -> void {
    if (!active_edge_is_linked(state, edge) || edge.top_point.y != top_y) { return; }

    edge.current_x = edge.top_point.x;
    if (active_is_maxima(edge)) {
        static_cast<void>(do_maxima(state, edge, options, succeeded));
        return;
    }

    if (is_hot_edge(edge)) { add_output_point(edge, edge.top_point); }
    update_scanbeam_edge(state, edge, options.preserve_collinear, succeeded);
    if (is_horizontal(edge)) { push_horizontal(state.sel_, edge); }
}

}  // namespace

auto do_top_of_scanbeam(clipper_base_state& state,
                        int64_t top_y,
                         const engine_scanbeam_orchestration_options& options,
                         bool& succeeded,
                         bool current_x_already_at_top) -> void {
    state.sel_ = nullptr;
    active_edge_node* edge = state.actives_;
    while (edge) {
        if (edge->top_point.y == top_y) {
            if (active_is_maxima(*edge)) {
                edge->current_x = edge->top_point.x;
                edge = do_maxima(state, *edge, options, succeeded);
                continue;
            }

            do_top_edge(state, *edge, top_y, options, succeeded);
        } else {
            if (!current_x_already_at_top) { edge->current_x = top_x(*edge, top_y); }
        }

        edge = edge->next_in_ael;
    }
}

auto do_known_top_edges_of_scanbeam(clipper_base_state& state,
                                    int64_t top_y,
                                    std::span<active_edge_node* const> top_edges,
                                     const engine_scanbeam_orchestration_options& options,
                                     bool& succeeded) -> void {
    state.sel_ = nullptr;
    for (auto* edge : top_edges) {
        if (edge) { do_top_edge(state, *edge, top_y, options, succeeded); }
    }
}

auto do_maxima(clipper_base_state& state,
               active_edge_node& edge,
               const engine_scanbeam_orchestration_options& options,
                bool& succeeded) -> active_edge_node* {
    active_edge_node* previous_edge = edge.prev_in_ael;
    active_edge_node* next_edge = edge.next_in_ael;
    if (is_open_end(edge)) {
        if (is_hot_edge(edge)) { add_output_point(edge, edge.top_point); }
        if (!is_horizontal(edge)) {
            if (is_hot_edge(edge)) {
                if (is_front(edge)) {
                    edge.outrec->front_edge = nullptr;
                } else {
                    edge.outrec->back_edge = nullptr;
                }
                edge.outrec = nullptr;
            }
            remove_from_ael(edge, state.actives_);
        }
        return next_edge;
    }

    active_edge_node* max_pair = maxima_pair(edge);
    if (!max_pair) { return next_edge; }

    if (is_joined(edge)) { split_joined_edge(state, edge, edge.top_point); }
    if (is_joined(*max_pair)) { split_joined_edge(state, *max_pair, max_pair->top_point); }

    while (next_edge != max_pair) {
        intersect_edges(state,
                        options.has_open_paths,
                        succeeded,
                        edge,
                        *next_edge,
                        edge.top_point);
        swap_positions_in_ael(edge, *next_edge, state.actives_);
        next_edge = edge.next_in_ael.get();
    }

    if (is_open(edge)) {
        if (is_hot_edge(edge)) {
            add_local_max_poly(state, edge, *max_pair, edge.top_point, succeeded);
        }
        remove_from_ael(*max_pair, state.actives_);
        remove_from_ael(edge, state.actives_);
        return previous_edge ? previous_edge->next_in_ael.get() : state.actives_.get();
    }

    if (is_hot_edge(edge)) {
        add_local_max_poly(state, edge, *max_pair, edge.top_point, succeeded);
    }

    remove_from_ael(edge, state.actives_);
    remove_from_ael(*max_pair, state.actives_);
    return previous_edge ? previous_edge->next_in_ael.get() : state.actives_.get();
}

}  // namespace clipper2next::internal
