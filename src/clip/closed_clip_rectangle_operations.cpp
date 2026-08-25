#include "clip/private/closed_clip_rectangle_operations.h"

#include <algorithm>
#include <utility>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto intervals_have_strict_overlap(int64_t first_begin,
                                                 int64_t first_end,
                                                 int64_t second_begin,
                                                 int64_t second_end) -> bool {
    return (std::max)(first_begin, second_begin) < (std::min)(first_end, second_end);
}

[[nodiscard]] auto rect_area(const Rect64& rect) -> long double {
    if (rect.is_empty()) { return 0.0L; }
    return static_cast<long double>(rect.width()) * static_cast<long double>(rect.height());
}

[[nodiscard]] auto non_strict_intersection_rect(const Rect64& first, const Rect64& second)
    -> Rect64 {
    return {(std::max)(first.left, second.left),
            (std::max)(first.top, second.top),
            (std::min)(first.right, second.right),
            (std::min)(first.bottom, second.bottom)};
}

[[nodiscard]] auto try_build_rectangular_union(
    const Rect64& first, const Rect64& second, Path64& path) -> bool {
    const auto union_rect = first.union_bounds(second);
    const auto intersection_rect = non_strict_intersection_rect(first, second);
    const auto covered_area = rect_area(first) + rect_area(second) - rect_area(intersection_rect);
    if (covered_area != rect_area(union_rect)) { return false; }

    if (first.top == second.top && first.bottom == second.bottom &&
        (first.right == second.left || second.right == first.left)) {
        const auto& left_rect = first.left < second.left ? first : second;
        const auto& right_rect = first.left < second.left ? second : first;
        const auto shared_x = right_rect.left;
        path = {{shared_x, union_rect.top},
                {union_rect.right, union_rect.top},
                {union_rect.right, union_rect.bottom},
                {shared_x, union_rect.bottom},
                {left_rect.left, union_rect.bottom},
                {left_rect.left, union_rect.top}};
        return true;
    }

    if (first.left == second.left && first.right == second.right &&
        (first.bottom == second.top || second.bottom == first.top)) {
        const auto& top_rect = first.top < second.top ? first : second;
        const auto& bottom_rect = first.top < second.top ? second : first;
        const auto shared_y = bottom_rect.top;
        path = {{union_rect.right, union_rect.bottom},
                {union_rect.left, union_rect.bottom},
                {union_rect.left, shared_y},
                {union_rect.left, top_rect.top},
                {union_rect.right, top_rect.top},
                {union_rect.right, shared_y}};
        return true;
    }

    path = rectangle_clip_solution_path(union_rect);
    return true;
}

[[nodiscard]] auto try_build_top_left_bottom_right_union(
    const Rect64& top_left, const Rect64& bottom_right, Path64& path) -> bool {
    if (!(top_left.left < bottom_right.left && top_left.top < bottom_right.top &&
          top_left.right < bottom_right.right && top_left.bottom < bottom_right.bottom)) {
        return false;
    }
    path = {{top_left.right, bottom_right.top},
            {bottom_right.right, bottom_right.top},
            {bottom_right.right, bottom_right.bottom},
            {bottom_right.left, bottom_right.bottom},
            {bottom_right.left, top_left.bottom},
            {top_left.left, top_left.bottom},
            {top_left.left, top_left.top},
            {top_left.right, top_left.top}};
    return true;
}

[[nodiscard]] auto try_build_bottom_left_top_right_union(
    const Rect64& bottom_left, const Rect64& top_right, Path64& path) -> bool {
    if (!(bottom_left.left < top_right.left && bottom_left.top > top_right.top &&
          bottom_left.right < top_right.right && bottom_left.bottom > top_right.bottom)) {
        return false;
    }
    path = {{top_right.right, top_right.bottom},
            {bottom_left.right, top_right.bottom},
            {bottom_left.right, bottom_left.bottom},
            {bottom_left.left, bottom_left.bottom},
            {bottom_left.left, bottom_left.top},
            {top_right.left, bottom_left.top},
            {top_right.left, top_right.top},
            {top_right.right, top_right.top}};
    return true;
}

}  // namespace

auto rectangles_have_strict_overlap(const Rect64& first, const Rect64& second) -> bool {
    return (std::max)(first.left, second.left) < (std::min)(first.right, second.right) &&
           (std::max)(first.top, second.top) < (std::min)(first.bottom, second.bottom);
}

auto rectangles_share_edge_segment(const Rect64& first, const Rect64& second) -> bool {
    const auto vertical_edge_touch =
        (first.right == second.left || second.right == first.left) &&
        intervals_have_strict_overlap(first.top, first.bottom, second.top, second.bottom);
    const auto horizontal_edge_touch =
        (first.bottom == second.top || second.bottom == first.top) &&
        intervals_have_strict_overlap(first.left, first.right, second.left, second.right);
    return vertical_edge_touch || horizontal_edge_touch;
}

auto strict_intersection_rect(const Rect64& first, const Rect64& second)
    -> std::optional<Rect64> {
    const auto left = (std::max)(first.left, second.left);
    const auto top = (std::max)(first.top, second.top);
    const auto right = (std::min)(first.right, second.right);
    const auto bottom = (std::min)(first.bottom, second.bottom);
    if (left >= right || top >= bottom) { return std::nullopt; }
    return Rect64{left, top, right, bottom};
}

auto rectangle_clip_solution_path(const Rect64& rect) -> Path64 {
    return {{rect.right, rect.bottom},
            {rect.left, rect.bottom},
            {rect.left, rect.top},
            {rect.right, rect.top}};
}

auto try_build_two_rectangle_union(
    const Rect64& first, const Rect64& second, Path64& path) -> bool {
    if (try_build_rectangular_union(first, second, path)) { return true; }
    if (!rectangles_have_strict_overlap(first, second)) { return false; }
    return try_build_top_left_bottom_right_union(first, second, path) ||
           try_build_top_left_bottom_right_union(second, first, path) ||
           try_build_bottom_left_top_right_union(first, second, path) ||
           try_build_bottom_left_top_right_union(second, first, path);
}

auto try_build_rectangle_corner_difference(
    const Rect64& subject, const Rect64& clip, Path64& path) -> bool {
    const auto intersection = strict_intersection_rect(subject, clip);
    if (!intersection) { return false; }
    if (*intersection == subject) {
        path.clear();
        return true;
    }

    const auto touches_left = intersection->left == subject.left;
    const auto touches_top = intersection->top == subject.top;
    const auto touches_right = intersection->right == subject.right;
    const auto touches_bottom = intersection->bottom == subject.bottom;
    const auto touched_sides = static_cast<int>(touches_left) + static_cast<int>(touches_top) +
                               static_cast<int>(touches_right) + static_cast<int>(touches_bottom);
    if (touched_sides != 2) { return false; }

    if (touches_right && touches_bottom) {
        path = {{subject.right, intersection->top},
                {intersection->left, intersection->top},
                {intersection->left, subject.bottom},
                {subject.left, subject.bottom},
                {subject.left, subject.top},
                {subject.right, subject.top}};
        return true;
    }
    if (touches_right && touches_top) {
        path = {{intersection->left, intersection->bottom},
                {subject.right, intersection->bottom},
                {subject.right, subject.bottom},
                {subject.left, subject.bottom},
                {subject.left, subject.top},
                {intersection->left, subject.top}};
        return true;
    }
    if (touches_left && touches_top) {
        path = {{subject.right, subject.bottom},
                {subject.left, subject.bottom},
                {subject.left, intersection->bottom},
                {intersection->right, intersection->bottom},
                {intersection->right, subject.top},
                {subject.right, subject.top}};
        return true;
    }
    if (touches_left && touches_bottom) {
        path = {{subject.right, subject.bottom},
                {intersection->right, subject.bottom},
                {intersection->right, intersection->top},
                {subject.left, intersection->top},
                {subject.left, subject.top},
                {subject.right, subject.top}};
        return true;
    }
    return false;
}

}  // namespace clipper2next::internal
