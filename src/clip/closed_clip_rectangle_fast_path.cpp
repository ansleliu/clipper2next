#include "clip/private/closed_clip_rectangle_operations.h"

namespace clipper2next::internal {

auto execute_two_rectangle_intersection(const Rect64& subject,
                                       const Rect64& clip,
                                       paths64_result& result) -> void {
    const auto intersection = strict_intersection_rect(subject, clip);
    if (intersection) {
        const std::array layouts{rectangle_clip_solution_layout(*intersection)};
        materialize_rectangle_paths(result, layouts);
    } else {
        materialize_rectangle_paths(result, std::array<rectangle_path_layout, 0>{});
    }
}

auto try_execute_two_rectangle_union(const Rect64& subject,
                                     const Rect64& clip,
                                     paths64_result& result) -> bool {
    std::array<rectangle_path_layout, 1> layouts;
    if (!try_layout_two_rectangle_union(subject, clip, layouts[0])) {
        if (rectangles_have_strict_overlap(subject, clip) ||
            rectangles_share_edge_segment(subject, clip)) {
            return false;
        }
        const std::array disjoint{
            rectangle_clip_solution_layout(subject), rectangle_clip_solution_layout(clip)};
        materialize_rectangle_paths(result, disjoint);
        return true;
    }

    materialize_rectangle_paths(result, layouts);
    return true;
}

auto try_execute_two_rectangle_difference(const Rect64& subject,
                                          const Rect64& clip,
                                          paths64_result& result) -> bool {
    if (!strict_intersection_rect(subject, clip)) {
        if (rectangles_share_edge_segment(subject, clip)) { return false; }
        const std::array layouts{rectangle_clip_solution_layout(subject)};
        materialize_rectangle_paths(result, layouts);
        return true;
    }
    std::array<rectangle_path_layout, 1> layouts;
    if (!try_layout_rectangle_corner_difference(subject, clip, layouts[0])) {
        return false;
    }

    if (layouts[0].point_count() != 0U) {
        materialize_rectangle_paths(result, layouts);
    } else {
        materialize_rectangle_paths(result, std::array<rectangle_path_layout, 0>{});
    }
    return true;
}

}  // namespace clipper2next::internal
