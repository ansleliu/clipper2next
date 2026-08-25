#include "clip/engine/private/engine_scanbeam_schedule.h"

#include "clip/engine/private/engine_geometry.h"

namespace clipper2next::internal {
namespace {

auto prepare_scanbeam_coordinates(active_edge_node& head,
                                  int64_t top_y,
                                  std::vector<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result {
    global_scanbeam_schedule_result result;
    top_edges.clear();

    auto* edge = &head;
    int64_t previous_x = top_x(*edge, top_y);
    edge->current_x = previous_x;
    edge->scanbeam_top_x = previous_x;
    if (edge->top_point.y == top_y) { top_edges.push_back(edge); }
    result.active_edge_count = 1U;

    for (edge = edge->next_in_ael.get(); edge != nullptr; edge = edge->next_in_ael.get()) {
        const int64_t current_x = top_x(*edge, top_y);
        edge->current_x = current_x;
        edge->scanbeam_top_x = current_x;
        if (edge->top_point.y == top_y) { top_edges.push_back(edge); }

        if (current_x < previous_x) {
            result.has_inversion = true;
            ++result.inversion_count;
        }
        previous_x = current_x;
        ++result.active_edge_count;
    }
    return result;
}

}  // namespace

auto prepare_global_scanbeam_schedule(active_edge_node& head,
                                      int64_t top_y,
                                      active_edge_node_ref& sorted_edges,
                                      std::vector<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result {
    return prepare_global_scanbeam_schedule(
        head, top_y, sorted_edges, top_edges, scanbeam_schedule_mode::unit_runs);
}

auto prepare_global_scanbeam_schedule(active_edge_node& head,
                                      int64_t top_y,
                                      active_edge_node_ref& sorted_edges,
                                      std::vector<active_edge_node*>& top_edges,
                                      scanbeam_schedule_mode schedule_mode)
    -> global_scanbeam_schedule_result {
    sorted_edges = nullptr;
    auto result = prepare_scanbeam_coordinates(head, top_y, top_edges);

    if (!result.has_inversion) { return result; }

    sorted_edges = &head;
    active_edge_node* previous = nullptr;
    active_edge_node* previous_run_start = &head;
    int64_t previous_x = 0;
    bool has_previous = false;
    for (active_edge_node* edge = &head; edge != nullptr; edge = edge->next_in_ael.get()) {
        edge->prev_in_sel = previous;
        edge->next_in_sel = edge->next_in_ael;
        edge->jump =
            schedule_mode == scanbeam_schedule_mode::unit_runs ? edge->next_in_sel : nullptr;

        if (schedule_mode == scanbeam_schedule_mode::monotone_runs && has_previous &&
            edge->current_x < previous_x) {
            previous_run_start->jump = edge;
            previous_run_start = edge;
        }

        previous_x = edge->current_x;
        has_previous = true;
        previous = edge;
    }

    return result;
}

auto prepare_contiguous_unit_scanbeam_schedule(
    active_edge_node& head,
    int64_t top_y,
    std::vector<active_edge_node*>& active_edges,
    std::vector<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result {
    active_edges.clear();
    auto result = prepare_scanbeam_coordinates(head, top_y, top_edges);
    if (!result.has_inversion) { return result; }

    active_edges.reserve(result.active_edge_count);
    for (auto* edge = &head; edge != nullptr; edge = edge->next_in_ael.get()) {
        active_edges.push_back(edge);
    }
    return result;
}

}  // namespace clipper2next::internal
