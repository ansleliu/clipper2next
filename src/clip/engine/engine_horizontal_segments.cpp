#include "clip/engine/private/engine_horizontal.h"

#include <algorithm>

namespace clipper2next::internal {

auto compare_horizontal_segments(const horizontal_segment_node& first,
                                 const horizontal_segment_node& second) -> bool {
    if (!first.has_right_point() || !second.has_right_point()) { return first.has_right_point(); }
    return first.left_point().pt.x < second.left_point().pt.x;
}

auto set_horizontal_segment_heading_forward(horizontal_segment_node& segment,
                                            output_point_node& previous,
                                            output_point_node& next) noexcept -> bool {
    if (&previous == &next) { return false; }

    if (previous.pt.x < next.pt.x) {
        segment.set_left_point(previous);
        segment.set_right_point(next);
        segment.left_to_right = true;
    } else {
        segment.set_left_point(next);
        segment.set_right_point(previous);
        segment.left_to_right = false;
    }
    return true;
}

auto update_horizontal_segment(horizontal_segment_node& segment) noexcept -> bool {
    auto* output_point = &segment.left_point();
    auto* output_record = get_real_outrec(output_point->outrec);
    const auto output_record_has_edges = output_record->front_edge != nullptr;
    const auto current_y = output_point->pt.y;
    auto* previous = output_point;
    auto* next = output_point;
    if (output_record_has_edges) {
        auto* first = output_record->pts.get();
        auto* last = first->next.get();
        while (previous != last && previous->prev->pt.y == current_y) {
            previous = previous->prev.get();
        }
        while (next != first && next->next->pt.y == current_y) { next = next->next.get(); }
    } else {
        while (previous->prev != next && previous->prev->pt.y == current_y) {
            previous = previous->prev.get();
        }
        while (next->next != previous && next->next->pt.y == current_y) { next = next->next.get(); }
    }

    const auto result = set_horizontal_segment_heading_forward(segment, *previous, *next) &&
                        !segment.left_point().has_horizontal_segment;

    if (result) {
        segment.left_point().has_horizontal_segment = true;
    } else {
        segment.clear_right_point();
    }
    return result;
}

auto sort_horizontal_segments(HorzSegmentList& segments) -> void {
    std::stable_sort(segments.begin(), segments.end(), compare_horizontal_segments);
}

}  // namespace clipper2next::internal
