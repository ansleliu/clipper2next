#include "clip/private/closed_clip_rectangle_operations.h"

namespace clipper2next::internal {

auto try_execute_two_rectangle_xor(const Rect64& subject,
                                  const Rect64& clip,
                                  paths64_result& result) -> bool {
    if (!strict_intersection_rect(subject, clip)) {
        std::array<rectangle_path_layout, 1> layouts;
        if (try_layout_rectangular_union(subject, clip, layouts[0])) {
            materialize_rectangle_paths(result, layouts);
        } else {
            if (rectangles_share_edge_segment(subject, clip)) { return false; }
            const std::array disjoint{
                rectangle_clip_solution_layout(subject), rectangle_clip_solution_layout(clip)};
            materialize_rectangle_paths(result, disjoint);
        }
        return true;
    }

    std::array<rectangle_path_layout, 2> differences;
    if (!try_layout_rectangle_corner_difference(subject, clip, differences[0]) ||
        !try_layout_rectangle_corner_difference(clip, subject, differences[1])) {
        return false;
    }
    if (differences[0].point_count() == 0U || differences[1].point_count() == 0U) {
        return false;
    }
    materialize_rectangle_paths(result, differences);
    return true;
}

}  // namespace clipper2next::internal
