#include "clip/engine/private/engine_horizontal.h"

namespace clipper2next::internal {

auto get_extended_horizontal_segment(output_point_node*& first, output_point_node*& second) noexcept
    -> bool {
    auto* output_record = get_real_outrec(first->outrec);
    second = first;
    if (output_record->front_edge) {
        while (first->prev != output_record->pts && first->prev->pt.y == first->pt.y) {
            first = first->prev.get();
        }
        while (second != output_record->pts && second->next->pt.y == second->pt.y) {
            second = second->next.get();
        }
        return second != first;
    }

    while (first->prev != second && first->prev->pt.y == first->pt.y) { first = first->prev.get(); }
    while (second->next != first && second->next->pt.y == second->pt.y) {
        second = second->next.get();
    }
    return second != first && second->next != first;
}

}  // namespace clipper2next::internal
