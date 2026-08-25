#pragma once

#include "clip/engine/private/engine_types.h"
#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {

inline auto swap_actives(active_edge_node*& first, active_edge_node*& second) noexcept -> void {
    auto* temp = first;
    first = second;
    second = temp;
}

inline auto extract_from_sel(active_edge_node* active) noexcept -> active_edge_node* {
    auto* result = active->next_in_sel.get();
    if (result) { result->prev_in_sel = active->prev_in_sel; }
    active->prev_in_sel->next_in_sel = result;
    active->prev_in_sel = nullptr;
    active->next_in_sel = nullptr;
    return result;
}

inline auto insert_before_in_sel(active_edge_node* first, active_edge_node* second) noexcept
    -> void {
    first->prev_in_sel = second->prev_in_sel;
    if (first->prev_in_sel) { first->prev_in_sel->next_in_sel = first; }
    first->next_in_sel = second;
    second->prev_in_sel = first;
}

inline auto insert_before_in_ael(active_edge_node& edge,
                                 active_edge_node& before,
                                 active_edge_node*& head) noexcept -> void {
    edge.prev_in_ael = before.prev_in_ael;
    edge.next_in_ael = &before;
    if (edge.prev_in_ael) {
        edge.prev_in_ael->next_in_ael = &edge;
    } else {
        head = &edge;
    }
    before.prev_in_ael = &edge;
}

inline auto insert_before_in_ael(active_edge_node& edge,
                                 active_edge_node& before,
                                 active_edge_node_ref& head) noexcept -> void {
    auto* raw_head = head.get();
    insert_before_in_ael(edge, before, raw_head);
    head = raw_head;
}

inline auto insert_right_edge(active_edge_node& left, active_edge_node& right) noexcept -> void {
    right.next_in_ael = left.next_in_ael;
    if (left.next_in_ael) { left.next_in_ael->prev_in_ael = &right; }
    right.prev_in_ael = &left;
    left.next_in_ael = &right;
}

inline auto unlink_from_ael(active_edge_node& edge, active_edge_node*& head) noexcept -> void {
    auto* previous = edge.prev_in_ael.get();
    auto* next = edge.next_in_ael.get();
    if (previous) {
        previous->next_in_ael = next;
    } else {
        head = next;
    }
    if (next) { next->prev_in_ael = previous; }
    edge.prev_in_ael = nullptr;
    edge.next_in_ael = nullptr;
}

inline auto unlink_from_ael(active_edge_node& edge, active_edge_node_ref& head) noexcept -> void {
    auto* raw_head = head.get();
    unlink_from_ael(edge, raw_head);
    head = raw_head;
}

inline auto remove_from_ael(active_edge_node& edge, active_edge_node*& head) -> bool {
    if (!edge.prev_in_ael && !edge.next_in_ael && (&edge != head)) { return false; }
    unlink_from_ael(edge, head);
    return true;
}

inline auto remove_from_ael(active_edge_node& edge, active_edge_node_ref& head) -> bool {
    auto* raw_head = head.get();
    const auto result = remove_from_ael(edge, raw_head);
    head = raw_head;
    return result;
}

inline auto swap_positions_in_ael(active_edge_node& first,
                                  active_edge_node& second,
                                  active_edge_node*& head) noexcept -> void {
    auto* next = second.next_in_ael.get();
    if (next) { next->prev_in_ael = &first; }
    auto* previous = first.prev_in_ael.get();
    if (previous) { previous->next_in_ael = &second; }
    second.prev_in_ael = previous;
    second.next_in_ael = &first;
    first.prev_in_ael = &second;
    first.next_in_ael = next;
    if (!second.prev_in_ael) { head = &second; }
}

inline auto swap_positions_in_ael(active_edge_node& first,
                                  active_edge_node& second,
                                  active_edge_node_ref& head) noexcept -> void {
    auto* raw_head = head.get();
    swap_positions_in_ael(first, second, raw_head);
    head = raw_head;
}

inline auto active_is_maxima(const Vertex& vertex) noexcept -> bool {
    return (vertex.flags & VertexFlags::LocalMax) != VertexFlags::Empty;
}

inline auto active_is_maxima(const active_edge_node& edge) noexcept -> bool {
    return active_is_maxima(*edge.vertex_top);
}

inline auto next_vertex(const active_edge_node& edge) noexcept -> Vertex* {
    return edge.wind_dx > 0 ? edge.vertex_top->next : edge.vertex_top->prev;
}

inline auto prev_prev_vertex(const active_edge_node& edge) noexcept -> Vertex* {
    return edge.wind_dx > 0 ? edge.vertex_top->prev->prev : edge.vertex_top->next->next;
}

inline auto is_valid_ael_order(const active_edge_node& resident, const active_edge_node& newcomer)
    -> bool {
    if (newcomer.current_x != resident.current_x) {
        return newcomer.current_x > resident.current_x;
    }

    const auto turn = cross_product_sign_in_clipper_range(
        resident.top_point, newcomer.bottom, newcomer.top_point);
    if (turn != 0) { return turn < 0; }

    if (!active_is_maxima(resident) && resident.top_point.y > newcomer.top_point.y) {
        return cross_product_sign_in_clipper_range(
                   newcomer.bottom, resident.top_point, next_vertex(resident)->pt) <= 0;
    }
    if (!active_is_maxima(newcomer) && newcomer.top_point.y > resident.top_point.y) {
        return cross_product_sign_in_clipper_range(
                   newcomer.bottom, newcomer.top_point, next_vertex(newcomer)->pt) >= 0;
    }

    const auto y = newcomer.bottom.y;
    const auto newcomer_is_left = newcomer.is_left_bound;
    if (resident.bottom.y != y || resident.local_min->vertex.get().pt.y != y) {
        return newcomer.is_left_bound;
    }
    if (resident.is_left_bound != newcomer_is_left) { return newcomer_is_left; }
    if (cross_product_sign_in_clipper_range(
            prev_prev_vertex(resident)->pt, resident.bottom, resident.top_point) == 0) {
        return true;
    }
    return (cross_product_sign_in_clipper_range(prev_prev_vertex(resident)->pt,
                                                newcomer.bottom,
                                                prev_prev_vertex(newcomer)->pt) > 0) ==
           newcomer_is_left;
}

}  // namespace clipper2next::internal
