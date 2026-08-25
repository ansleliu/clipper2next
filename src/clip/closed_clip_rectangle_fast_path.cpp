#include "clip/private/closed_clip_fast_path.h"

#include "clip/private/closed_clip_rectangle_operations.h"

#include <utility>

namespace clipper2next::internal {

auto try_execute_two_rectangle_intersection(const clip_request64& request,
                                            const clip_request_metadata64& metadata,
                                            paths64_result& result) -> bool {
    if (request.clip_type != ClipType::Intersection || metadata.subject_path_count != 1U ||
        metadata.clip_path_count != 1U || !metadata.single_subject_rect ||
        !metadata.single_clip_rect) {
        return false;
    }
    const auto intersection =
        strict_intersection_rect(*metadata.single_subject_rect, *metadata.single_clip_rect);
    result.closed.clear();
    if (intersection) { result.closed.emplace_back(rectangle_clip_solution_path(*intersection)); }
    result.open.clear();
    return true;
}

auto try_execute_two_rectangle_union(const clip_request64& request,
                                     const clip_request_metadata64& metadata,
                                     paths64_result& result) -> bool {
    if (request.clip_type != ClipType::Union || metadata.subject_path_count != 1U ||
        metadata.clip_path_count != 1U || !metadata.single_subject_rect ||
        !metadata.single_clip_rect) {
        return false;
    }

    Path64 union_path;
    if (!try_build_two_rectangle_union(
            *metadata.single_subject_rect, *metadata.single_clip_rect, union_path)) {
        if (rectangles_have_strict_overlap(*metadata.single_subject_rect,
                                           *metadata.single_clip_rect) ||
            rectangles_share_edge_segment(*metadata.single_subject_rect,
                                          *metadata.single_clip_rect)) {
            return false;
        }
        result.closed = {rectangle_clip_solution_path(*metadata.single_subject_rect),
                         rectangle_clip_solution_path(*metadata.single_clip_rect)};
        result.open.clear();
        return true;
    }

    result.closed.clear();
    result.closed.emplace_back(std::move(union_path));
    result.open.clear();
    return true;
}

auto try_execute_two_rectangle_difference(const clip_request64& request,
                                          const clip_request_metadata64& metadata,
                                          paths64_result& result) -> bool {
    if (request.clip_type != ClipType::Difference || metadata.subject_path_count != 1U ||
        metadata.clip_path_count != 1U || !metadata.single_subject_rect ||
        !metadata.single_clip_rect) {
        return false;
    }

    Path64 difference_path;
    if (!strict_intersection_rect(*metadata.single_subject_rect, *metadata.single_clip_rect)) {
        result.closed = {rectangle_clip_solution_path(*metadata.single_subject_rect)};
        result.open.clear();
        return true;
    }
    if (!try_build_rectangle_corner_difference(
            *metadata.single_subject_rect, *metadata.single_clip_rect, difference_path)) {
        return false;
    }

    result.closed.clear();
    if (!difference_path.empty()) { result.closed.push_back(std::move(difference_path)); }
    result.open.clear();
    return true;
}

}  // namespace clipper2next::internal
