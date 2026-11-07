#include "clip/engine/private/engine_scanbeam_schedule.h"

#include "clip/engine/private/engine_geometry.h"

namespace clipper2next::internal {
namespace {

auto prepare_scanbeam_coordinates(active_edge_node& head,
                                  int64_t top_y,
                                  engine_buffer<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result {
    global_scanbeam_schedule_result result;
    top_edges.clear();

    int64_t previous_x = 0;
    const auto prepare_edge = [&](active_edge_node& edge) {
        const int64_t current_x = top_x(edge, top_y);
        edge.current_x = current_x;
        edge.scanbeam_top_x = current_x;
        if (result.active_edge_count != 0U && current_x < previous_x) {
            result.has_inversion = true;
            ++result.inversion_count;
        }
        previous_x = current_x;
        ++result.active_edge_count;
    };
    for (auto* edge = &head; edge != nullptr;) {
        if (edge->top_point.y != top_y) {
            prepare_edge(*edge);
            edge = edge->next_in_ael.get();
            continue;
        }
        auto* first = edge;
        auto* end = edge;
        auto count = std::size_t{};
        do {
            ++count;
            end = end->next_in_ael.get();
        } while (end && end->top_point.y == top_y);
        if (count > top_edges.capacity() - top_edges.size()) {
            // Allocation may call out and change the floating environment.
            // Keep the original projection/append order on physical growth.
            do {
                prepare_edge(*edge);
                top_edges.push_back(edge);
                edge = edge->next_in_ael.get();
            } while (edge != end);
            continue;
        }
        do {
            prepare_edge(*edge);
            edge = edge->next_in_ael.get();
        } while (edge != end);
        // This pass only changes coordinates in existing nodes. As in the
        // contiguous restore pass, append each ending run as one no-callout
        // growth; rejected bulk admission retains the exact sequential prefix.
        top_edges.append_generated(count, [&first]() noexcept {
            auto* current = first;
            first = first->next_in_ael.get();
            return current;
        });
    }
    return result;
}

}  // namespace

auto prepare_global_scanbeam_schedule(active_edge_node& head,
                                      int64_t top_y,
                                      active_edge_node_ref& sorted_edges,
                                      engine_buffer<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result {
    return prepare_global_scanbeam_schedule(
        head, top_y, sorted_edges, top_edges, scanbeam_schedule_mode::unit_runs);
}

auto prepare_global_scanbeam_schedule(active_edge_node& head,
                                      int64_t top_y,
                                      active_edge_node_ref& sorted_edges,
                                      engine_buffer<active_edge_node*>& top_edges,
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
    engine_buffer<active_edge_node*>& active_edges,
    engine_buffer<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result {
    active_edges.clear();
    auto result = prepare_scanbeam_coordinates(head, top_y, top_edges);
    if (!result.has_inversion) { return result; }

    active_edges.reserve(result.active_edge_count);
    auto* edge = &head;
    active_edges.append_generated(result.active_edge_count, [&edge]() noexcept {
        auto* current = edge;
        edge = edge->next_in_ael.get();
        return current;
    });
    return result;
}

auto restore_contiguous_scanbeam_coordinates(
    int64_t top_y,
    std::span<active_edge_node* const> active_edges,
    engine_buffer<active_edge_node*>& top_edges) -> void {
    top_edges.clear();
    auto first = std::size_t{};
    while (first < active_edges.size()) {
        auto* edge = active_edges[first];
        edge->current_x = edge->scanbeam_top_x;
        if (edge->top_point.y != top_y) { ++first; continue; }
        auto last = first + 1U;
        while (last < active_edges.size() &&
               active_edges[last]->top_point.y == top_y) {
            auto* ending = active_edges[last];
            ending->current_x = ending->scanbeam_top_x;
            ++last;
        }
        top_edges.append_range(
            std::span<active_edge_node* const>{active_edges.data() + first, last - first});
        first = last;
    }
}

}  // namespace clipper2next::internal
