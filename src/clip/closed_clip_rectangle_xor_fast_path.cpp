#include "clip/private/closed_clip_fast_path.h"

#include "clipper2next/core/rect.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto strict_intersection_rect(const Rect64& first, const Rect64& second)
    -> std::optional<Rect64> {
    const auto left = (std::max)(first.left, second.left);
    const auto top = (std::max)(first.top, second.top);
    const auto right = (std::min)(first.right, second.right);
    const auto bottom = (std::min)(first.bottom, second.bottom);
    if (left >= right || top >= bottom) { return std::nullopt; }
    return Rect64{left, top, right, bottom};
}

[[nodiscard]] auto intervals_have_strict_overlap(int64_t first_begin,
                                                 int64_t first_end,
                                                 int64_t second_begin,
                                                 int64_t second_end) -> bool {
    return (std::max)(first_begin, second_begin) < (std::min)(first_end, second_end);
}

[[nodiscard]] auto rectangles_share_edge_segment(const Rect64& first, const Rect64& second)
    -> bool {
    const auto vertical_edge_touch =
        (first.right == second.left || second.right == first.left) &&
        intervals_have_strict_overlap(first.top, first.bottom, second.top, second.bottom);
    const auto horizontal_edge_touch =
        (first.bottom == second.top || second.bottom == first.top) &&
        intervals_have_strict_overlap(first.left, first.right, second.left, second.right);
    return vertical_edge_touch || horizontal_edge_touch;
}

[[nodiscard]] auto rect_area(const Rect64& rect) -> long double {
    if (rect.is_empty()) { return 0.0L; }
    return static_cast<long double>(rect.width()) * static_cast<long double>(rect.height());
}

[[nodiscard]] auto non_strict_intersection_rect(const Rect64& first, const Rect64& second)
    -> Rect64 {
    return Rect64{
        (std::max)(first.left, second.left),
        (std::max)(first.top, second.top),
        (std::min)(first.right, second.right),
        (std::min)(first.bottom, second.bottom),
    };
}

[[nodiscard]] auto rectangle_clip_solution_path(const Rect64& rect) -> Path64 {
    Path64 result;
    result.reserve(4U);
    result.emplace_back(rect.right, rect.bottom);
    result.emplace_back(rect.left, rect.bottom);
    result.emplace_back(rect.left, rect.top);
    result.emplace_back(rect.right, rect.top);
    return result;
}

[[nodiscard]] auto try_build_rectangular_union(const Rect64& first,
                                               const Rect64& second,
                                               Path64& path) -> bool {
    const auto union_rect = first.union_bounds(second);
    const auto intersection_rect = non_strict_intersection_rect(first, second);
    const auto covered_area = rect_area(first) + rect_area(second) - rect_area(intersection_rect);
    if (covered_area != rect_area(union_rect)) { return false; }

    if (first.top == second.top && first.bottom == second.bottom &&
        (first.right == second.left || second.right == first.left)) {
        const auto& left_rect = first.left < second.left ? first : second;
        const auto& right_rect = first.left < second.left ? second : first;
        const auto shared_x = right_rect.left;
        Path64 result;
        result.reserve(6U);
        result.emplace_back(shared_x, union_rect.top);
        result.emplace_back(union_rect.right, union_rect.top);
        result.emplace_back(union_rect.right, union_rect.bottom);
        result.emplace_back(shared_x, union_rect.bottom);
        result.emplace_back(left_rect.left, union_rect.bottom);
        result.emplace_back(left_rect.left, union_rect.top);
        path = std::move(result);
        return true;
    }

    if (first.left == second.left && first.right == second.right &&
        (first.bottom == second.top || second.bottom == first.top)) {
        const auto& top_rect = first.top < second.top ? first : second;
        const auto& bottom_rect = first.top < second.top ? second : first;
        const auto shared_y = bottom_rect.top;
        Path64 result;
        result.reserve(6U);
        result.emplace_back(union_rect.right, union_rect.bottom);
        result.emplace_back(union_rect.left, union_rect.bottom);
        result.emplace_back(union_rect.left, shared_y);
        result.emplace_back(union_rect.left, top_rect.top);
        result.emplace_back(union_rect.right, top_rect.top);
        result.emplace_back(union_rect.right, shared_y);
        path = std::move(result);
        return true;
    }

    path = rectangle_clip_solution_path(union_rect);
    return true;
}

[[nodiscard]] auto try_build_rectangle_corner_difference(const Rect64& subject,
                                                         const Rect64& clip,
                                                         Path64& path) -> bool {
    const auto intersection = strict_intersection_rect(subject, clip);
    if (!intersection || *intersection == subject) { return false; }

    const auto touches_left = intersection->left == subject.left;
    const auto touches_top = intersection->top == subject.top;
    const auto touches_right = intersection->right == subject.right;
    const auto touches_bottom = intersection->bottom == subject.bottom;
    const auto touched_sides = static_cast<int>(touches_left) + static_cast<int>(touches_top) +
                               static_cast<int>(touches_right) + static_cast<int>(touches_bottom);
    if (touched_sides != 2) { return false; }

    if (touches_right && touches_bottom) {
        path = Path64{{subject.right, intersection->top},
                      {intersection->left, intersection->top},
                      {intersection->left, subject.bottom},
                      {subject.left, subject.bottom},
                      {subject.left, subject.top},
                      {subject.right, subject.top}};
        return true;
    }
    if (touches_right && touches_top) {
        path = Path64{{intersection->left, intersection->bottom},
                      {subject.right, intersection->bottom},
                      {subject.right, subject.bottom},
                      {subject.left, subject.bottom},
                      {subject.left, subject.top},
                      {intersection->left, subject.top}};
        return true;
    }
    if (touches_left && touches_top) {
        path = Path64{{subject.right, subject.bottom},
                      {subject.left, subject.bottom},
                      {subject.left, intersection->bottom},
                      {intersection->right, intersection->bottom},
                      {intersection->right, subject.top},
                      {subject.right, subject.top}};
        return true;
    }
    if (touches_left && touches_bottom) {
        path = Path64{{subject.right, subject.bottom},
                      {intersection->right, subject.bottom},
                      {intersection->right, intersection->top},
                      {subject.left, intersection->top},
                      {subject.left, subject.top},
                      {subject.right, subject.top}};
        return true;
    }
    return false;
}

}  // namespace

auto try_execute_two_rectangle_xor(const clip_request64& request,
                                   const clip_request_metadata64& metadata,
                                   paths64_result& result) -> bool {
    if (request.clip_type != ClipType::Xor || metadata.subject_path_count != 1U ||
        metadata.clip_path_count != 1U || !metadata.single_subject_rect ||
        !metadata.single_clip_rect) {
        return false;
    }

    if (!strict_intersection_rect(*metadata.single_subject_rect, *metadata.single_clip_rect)) {
        Path64 union_path;
        result.closed.clear();
        if (try_build_rectangular_union(
                *metadata.single_subject_rect, *metadata.single_clip_rect, union_path)) {
            result.closed.push_back(std::move(union_path));
        } else {
            if (rectangles_share_edge_segment(*metadata.single_subject_rect,
                                              *metadata.single_clip_rect)) {
                return false;
            }
            result.closed.reserve(2U);
            result.closed.emplace_back(
                rectangle_clip_solution_path(*metadata.single_subject_rect));
            result.closed.emplace_back(
                rectangle_clip_solution_path(*metadata.single_clip_rect));
        }
        result.open.clear();
        return true;
    }

    Path64 subject_difference;
    Path64 clip_difference;
    if (!try_build_rectangle_corner_difference(
            *metadata.single_subject_rect, *metadata.single_clip_rect, subject_difference) ||
        !try_build_rectangle_corner_difference(
            *metadata.single_clip_rect, *metadata.single_subject_rect, clip_difference)) {
        return false;
    }

    result.closed.clear();
    result.closed.reserve(2U);
    result.closed.push_back(std::move(subject_difference));
    result.closed.push_back(std::move(clip_difference));
    result.open.clear();
    return true;
}

}  // namespace clipper2next::internal
