#include "clip/engine/private/engine_output_topology.h"

namespace clipper2next::internal {

auto join_outrec_paths(active_edge_node& first, active_edge_node& second) -> void {
    auto* first_start = first.outrec->pts.get();
    auto* second_start = second.outrec->pts.get();
    auto* first_end = first_start->next.get();
    auto* second_end = second_start->next.get();

    if (is_front(first)) {
        second_end->prev = first_start;
        first_start->next = second_end;
        second_start->next = first_end;
        first_end->prev = second_start;
        first.outrec->pts = second_start;
        first.outrec->front_edge = second.outrec->front_edge;
        if (first.outrec->front_edge) { first.outrec->front_edge->outrec = first.outrec; }
    } else {
        first_end->prev = second_start;
        second_start->next = first_end;
        first_start->next = second_end;
        second_end->prev = first_start;
        first.outrec->back_edge = second.outrec->back_edge;
        if (first.outrec->back_edge) { first.outrec->back_edge->outrec = first.outrec; }
    }

    second.outrec->front_edge = nullptr;
    second.outrec->back_edge = nullptr;
    second.outrec->pts = nullptr;

    if (is_open_end(first)) {
        second.outrec->pts = first.outrec->pts;
        first.outrec->pts = nullptr;
    } else {
        set_owner(second.outrec, first.outrec);
    }

    first.outrec = nullptr;
    second.outrec = nullptr;
}

}  // namespace clipper2next::internal
