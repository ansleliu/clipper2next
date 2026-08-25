#include "clip/engine/private/engine_horizontal.h"

#include "clip/engine/private/engine_output.h"

#include <algorithm>

namespace clipper2next::internal {

auto convert_horizontal_segments_to_joins(HorzSegmentList& horizontal_segments,
                                          std::vector<horizontal_join_node>& horizontal_joins)
    -> void {
    const auto valid_count = std::count_if(
        horizontal_segments.begin(),
        horizontal_segments.end(),
        [](horizontal_segment_node& segment) { return update_horizontal_segment(segment); });
    if (valid_count < 2) { return; }

    sort_horizontal_segments(horizontal_segments);

    auto first = horizontal_segments.begin();
    const auto end = first + valid_count;
    const auto last_first = end - 1;

    for (; first != last_first; ++first) {
        for (auto second = first + 1; second != end; ++second) {
            auto& first_left_for_overlap = first->left_point();
            auto& first_right_for_overlap = first->right_point();
            auto& second_left_for_overlap = second->left_point();
            auto& second_right_for_overlap = second->right_point();
            if ((second_left_for_overlap.pt.x >= first_right_for_overlap.pt.x) ||
                (second->left_to_right == first->left_to_right) ||
                (second_right_for_overlap.pt.x <= first_left_for_overlap.pt.x)) {
                continue;
            }

            const auto current_y = first_left_for_overlap.pt.y;
            if (first->left_to_right) {
                while (first->left_point().next->pt.y == current_y &&
                       first->left_point().next->pt.x <= second->left_point().pt.x) {
                    first->set_left_point(*first->left_point().next);
                }
                while (second->left_point().prev->pt.y == current_y &&
                       second->left_point().prev->pt.x <= first->left_point().pt.x) {
                    second->set_left_point(*second->left_point().prev);
                }
                auto& first_left = first->left_point();
                auto& second_left = second->left_point();
                horizontal_joins.emplace_back(*duplicate_out_point(&first_left, true),
                                              *duplicate_out_point(&second_left, false));
            } else {
                while (first->left_point().prev->pt.y == current_y &&
                       first->left_point().prev->pt.x <= second->left_point().pt.x) {
                    first->set_left_point(*first->left_point().prev);
                }
                while (second->left_point().next->pt.y == current_y &&
                       second->left_point().next->pt.x <= first->left_point().pt.x) {
                    second->set_left_point(*second->left_point().next);
                }
                auto& first_left = first->left_point();
                auto& second_left = second->left_point();
                horizontal_joins.emplace_back(*duplicate_out_point(&second_left, true),
                                              *duplicate_out_point(&first_left, false));
            }
        }
    }
}

}  // namespace clipper2next::internal
