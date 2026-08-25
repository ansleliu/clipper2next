#pragma once

#include "clip/engine/private/engine_types.h"

namespace clipper2next::internal {

[[nodiscard]] inline auto is_front(const active_edge_node& edge) noexcept -> bool {
    return &edge == edge.outrec->front_edge;
}

inline auto set_sides(output_record_node& output_record,
                      active_edge_node& start_edge,
                      active_edge_node& end_edge) noexcept -> void {
    output_record.front_edge = &start_edge;
    output_record.back_edge = &end_edge;
}

inline auto swap_outrecs(active_edge_node& first, active_edge_node& second) noexcept -> void {
    auto* first_record = first.outrec.get();
    auto* second_record = second.outrec.get();
    if (first_record == second_record) {
        auto* edge = first_record->front_edge.get();
        first_record->front_edge = first_record->back_edge;
        first_record->back_edge = edge;
        return;
    }
    if (first_record) {
        if (&first == first_record->front_edge) {
            first_record->front_edge = &second;
        } else {
            first_record->back_edge = &second;
        }
    }
    if (second_record) {
        if (&second == second_record->front_edge) {
            second_record->front_edge = &first;
        } else {
            second_record->back_edge = &first;
        }
    }
    first.outrec = second_record;
    second.outrec = first_record;
}

inline auto reverse_out_points(output_point_node* output_point) noexcept -> void {
    if (!output_point) { return; }

    auto* current = output_point;
    do {
        auto* next = current->next.get();
        current->next = current->prev;
        current->prev = next;
        current = next;
    } while (current != output_point);
}

inline auto swap_sides(output_record_node& output_record) noexcept -> void {
    auto* edge = output_record.front_edge.get();
    output_record.front_edge = output_record.back_edge;
    output_record.back_edge = edge;
    output_record.pts = output_record.pts->next;
}

[[nodiscard]] inline auto get_real_outrec(output_record_node* output_record) noexcept
    -> output_record_node* {
    while (output_record && !output_record->pts) { output_record = output_record->owner; }
    return output_record;
}

[[nodiscard]] inline auto is_valid_owner(output_record_node* output_record,
                                         output_record_node* candidate_owner) noexcept -> bool {
    while (candidate_owner && candidate_owner != output_record) {
        candidate_owner = candidate_owner->owner;
    }
    return !candidate_owner;
}

inline auto uncouple_outrec(active_edge_node active) noexcept -> void {
    auto* output_record = active.outrec.get();
    if (!output_record) { return; }
    output_record->front_edge->outrec = nullptr;
    output_record->back_edge->outrec = nullptr;
    output_record->front_edge = nullptr;
    output_record->back_edge = nullptr;
}

inline auto set_owner(output_record_node* output_record, output_record_node* new_owner) noexcept
    -> void {
    new_owner->owner = get_real_outrec(new_owner->owner);
    auto* owner = new_owner;
    while (owner && owner != output_record) { owner = owner->owner.get(); }
    if (owner) { new_owner->owner = output_record->owner; }
    output_record->owner = new_owner;
}

[[nodiscard]] auto point_in_output_polygon(const Point64& point, output_point_node* output_point)
    -> PointInPolygonResult;

[[nodiscard]] auto get_clean_path(output_point_node* output_point) -> Path64;

[[nodiscard]] auto path2_contains_path1(output_point_node* first, output_point_node* second)
    -> bool;
auto move_splits(output_record_node* from, output_record_node* to) -> void;

inline auto update_outrec_owner(output_record_node* output_record) noexcept -> void {
    auto* current = output_record->pts.get();
    for (;;) {
        current->outrec = output_record;
        current = current->next;
        if (current == output_record->pts) { return; }
    }
}

}  // namespace clipper2next::internal
