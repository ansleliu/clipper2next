#include "clip/engine/private/engine_horizontal.h"

namespace clipper2next::internal {

auto trim_horizontal(active_edge_node& horizontal_edge, bool preserve_collinear) noexcept -> void {
    auto was_trimmed = false;
    auto point = next_vertex(horizontal_edge)->pt;
    while (point.y == horizontal_edge.top_point.y) {
        if (preserve_collinear && ((point.x < horizontal_edge.top_point.x) !=
                                   (horizontal_edge.bottom.x < horizontal_edge.top_point.x))) {
            break;
        }

        horizontal_edge.vertex_top = next_vertex(horizontal_edge);
        horizontal_edge.top_point = point;
        was_trimmed = true;
        if (active_is_maxima(horizontal_edge)) { break; }
        point = next_vertex(horizontal_edge)->pt;
    }

    if (was_trimmed) {
        horizontal_edge.dx = get_dx(horizontal_edge.bottom, horizontal_edge.top_point);
    }
}

auto push_horizontal(active_edge_node*& horizontal_stack, active_edge_node& edge) noexcept -> void {
    edge.next_in_sel = horizontal_stack ? horizontal_stack : nullptr;
    horizontal_stack = &edge;
}

auto push_horizontal(active_edge_node_ref& horizontal_stack, active_edge_node& edge) noexcept
    -> void {
    auto* raw_stack = horizontal_stack.get();
    push_horizontal(raw_stack, edge);
    horizontal_stack = raw_stack;
}

auto pop_horizontal(active_edge_node*& horizontal_stack, active_edge_node*& edge) noexcept -> bool {
    edge = horizontal_stack;
    if (!edge) { return false; }
    horizontal_stack = horizontal_stack->next_in_sel;
    return true;
}

auto pop_horizontal(active_edge_node_ref& horizontal_stack, active_edge_node*& edge) noexcept
    -> bool {
    auto* raw_stack = horizontal_stack.get();
    const auto result = pop_horizontal(raw_stack, edge);
    horizontal_stack = raw_stack;
    return result;
}

auto reset_horizontal_direction(const active_edge_node& horizontal_edge,
                                const Vertex* max_vertex,
                                int64_t& horizontal_left,
                                int64_t& horizontal_right) noexcept -> bool {
    if (horizontal_edge.bottom.x == horizontal_edge.top_point.x) {
        horizontal_left = horizontal_edge.current_x;
        horizontal_right = horizontal_edge.current_x;
        auto* edge = horizontal_edge.next_in_ael.get();
        while (edge && edge->vertex_top != max_vertex) { edge = edge->next_in_ael.get(); }
        return edge != nullptr;
    }

    if (horizontal_edge.current_x < horizontal_edge.top_point.x) {
        horizontal_left = horizontal_edge.current_x;
        horizontal_right = horizontal_edge.top_point.x;
        return true;
    }

    horizontal_left = horizontal_edge.top_point.x;
    horizontal_right = horizontal_edge.current_x;
    return false;
}

auto add_trial_horizontal_join(HorzSegmentList& horizontal_segments,
                               output_point_node* output_point) -> void {
    if (output_point->outrec->is_open) { return; }
    horizontal_segments.emplace_back(*output_point);
}

}  // namespace clipper2next::internal
