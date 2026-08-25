#include "clip/engine/private/engine_output.h"

#include "clip/engine/private/engine_output_owner.h"

namespace clipper2next::internal {

auto point_count(output_point_node* output_point) -> int {
    auto* current = output_point;
    auto count = 0;
    do {
        current = current->next.get();
        ++count;
    } while (current != output_point);
    return count;
}

auto duplicate_out_point(output_point_node* output_point, bool insert_after) -> output_point_node* {
    auto* result =
        &output_point->outrec->output_owner->create_outpt(output_point->pt, *output_point->outrec);
    if (insert_after) {
        result->next = output_point->next;
        result->next->prev = result;
        result->prev = output_point;
        output_point->next = result;
    } else {
        result->prev = output_point->prev;
        result->prev->next = result;
        result->next = output_point;
        output_point->prev = result;
    }
    return result;
}

auto dispose_out_point(output_point_node* output_point) -> output_point_node* {
    auto* result = output_point->next.get();
    output_point->prev->next = output_point->next;
    output_point->next->prev = output_point->prev;
    output_point->next = output_point;
    output_point->prev = output_point;
    output_point->outrec = nullptr;
    return result;
}

auto dispose_out_points(output_record_node* output_record) -> void {
    auto* output_point = output_record->pts.get();
    auto* current = output_point;
    do {
        auto* next = current->next.get();
        current->next = current;
        current->prev = current;
        current->outrec = nullptr;
        current = next;
    } while (current != output_point);
    output_record->pts = nullptr;
}

}  // namespace clipper2next::internal
