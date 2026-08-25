#include "clip/engine/private/engine_ael_builder.h"

namespace clipper2next::internal {

auto insert_left_edge(clipper_base_state& state, active_edge_node& edge) -> void {
    if (!state.actives_) {
        edge.prev_in_ael = nullptr;
        edge.next_in_ael = nullptr;
        state.actives_ = &edge;
    } else if (!is_valid_ael_order(*state.actives_, edge)) {
        insert_before_in_ael(edge, *state.actives_, state.actives_);
    } else {
        active_edge_node* insert_after = state.actives_;
        while (insert_after->next_in_ael && is_valid_ael_order(*insert_after->next_in_ael, edge)) {
            insert_after = insert_after->next_in_ael;
        }
        if (insert_after->join_with == JoinWith::Right) {
            insert_after = insert_after->next_in_ael;
        }
        if (!insert_after) { return; }
        edge.next_in_ael = insert_after->next_in_ael;
        if (insert_after->next_in_ael) { insert_after->next_in_ael->prev_in_ael = &edge; }
        edge.prev_in_ael = insert_after;
        insert_after->next_in_ael = &edge;
    }
}

auto adjust_curr_x_and_copy_to_sel(clipper_base_state& state, int64_t top_y) -> void {
    active_edge_node* edge = state.actives_;
    state.sel_ = edge;
    while (edge) {
        edge->prev_in_sel = edge->prev_in_ael;
        edge->next_in_sel = edge->next_in_ael;
        edge->jump = edge->next_in_sel;
        edge->current_x = top_x(*edge, top_y);
        edge = edge->next_in_ael;
    }
}

}  // namespace clipper2next::internal
