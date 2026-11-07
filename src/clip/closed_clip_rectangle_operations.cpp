#include "clip/private/closed_clip_rectangle_operations.h"
#include "geometry/private/rectangle_certificate.h"
#include <algorithm>
#include <array>
#include <span>

namespace clipper2next::internal {
namespace {
auto intervals_overlap(int64_t first_begin,
                       int64_t first_end,
                       int64_t second_begin,
                       int64_t second_end) -> bool {
    return (std::max)(first_begin, second_begin) < (std::min)(first_end, second_end);
}
using x = rectangle_x;
using y = rectangle_y;
constexpr auto rectangle_vertices =
    std::to_array<rectangle_vertex>({{x::first_right, y::first_bottom},
                                     {x::first_left, y::first_bottom},
                                     {x::first_left, y::first_top},
                                     {x::first_right, y::first_top}});
constexpr auto descending_union_vertices =
    std::to_array<rectangle_vertex>({{x::first_right, y::second_top},
                                     {x::second_right, y::second_top},
                                     {x::second_right, y::second_bottom},
                                     {x::second_left, y::second_bottom},
                                     {x::second_left, y::first_bottom},
                                     {x::first_left, y::first_bottom},
                                     {x::first_left, y::first_top},
                                     {x::first_right, y::first_top}});
constexpr auto ascending_union_vertices =
    std::to_array<rectangle_vertex>({{x::second_right, y::second_bottom},
                                     {x::first_right, y::second_bottom},
                                     {x::first_right, y::first_bottom},
                                     {x::first_left, y::first_bottom},
                                     {x::first_left, y::first_top},
                                     {x::second_left, y::first_top},
                                     {x::second_left, y::second_top},
                                     {x::second_right, y::second_top}});
constexpr auto difference_bottom_right_vertices =
    std::to_array<rectangle_vertex>({{x::first_right, y::second_top},
                                     {x::second_left, y::second_top},
                                     {x::second_left, y::first_bottom},
                                     {x::first_left, y::first_bottom},
                                     {x::first_left, y::first_top},
                                     {x::first_right, y::first_top}});
constexpr auto difference_top_right_vertices =
    std::to_array<rectangle_vertex>({{x::second_left, y::second_bottom},
                                     {x::first_right, y::second_bottom},
                                     {x::first_right, y::first_bottom},
                                     {x::first_left, y::first_bottom},
                                     {x::first_left, y::first_top},
                                     {x::second_left, y::first_top}});
constexpr auto difference_top_left_vertices =
    std::to_array<rectangle_vertex>({{x::first_right, y::first_bottom},
                                     {x::first_left, y::first_bottom},
                                     {x::first_left, y::second_bottom},
                                     {x::second_right, y::second_bottom},
                                     {x::second_right, y::first_top},
                                     {x::first_right, y::first_top}});
constexpr auto difference_bottom_left_vertices =
    std::to_array<rectangle_vertex>({{x::first_right, y::first_bottom},
                                     {x::second_right, y::first_bottom},
                                     {x::second_right, y::second_top},
                                     {x::first_left, y::second_top},
                                     {x::first_left, y::first_top},
                                     {x::first_right, y::first_top}});
}  // namespace

auto rectangles_have_strict_overlap(const Rect64& first, const Rect64& second) -> bool {
    return intervals_overlap(first.left, first.right, second.left, second.right) &&
           intervals_overlap(first.top, first.bottom, second.top, second.bottom);
}
auto rectangles_share_edge_segment(const Rect64& first, const Rect64& second) -> bool {
    return ((first.right == second.left || second.right == first.left) &&
            intervals_overlap(first.top, first.bottom, second.top, second.bottom)) ||
           ((first.bottom == second.top || second.bottom == first.top) &&
            intervals_overlap(first.left, first.right, second.left, second.right));
}
auto strict_intersection_rect(const Rect64& first, const Rect64& second) -> std::optional<Rect64> {
    const auto left = (std::max)(first.left, second.left);
    const auto top = (std::max)(first.top, second.top);
    const auto right = (std::min)(first.right, second.right);
    const auto bottom = (std::min)(first.bottom, second.bottom);
    if (left >= right || top >= bottom) { return std::nullopt; }
    return Rect64{left, top, right, bottom};
}
auto rectangle_clip_solution_layout(const Rect64& rect) -> rectangle_path_layout {
    return {rect, {}, rectangle_vertices};
}
auto try_layout_rectangular_union(const Rect64& first,
                                  const Rect64& second,
                                  rectangle_path_layout& layout) -> bool {
    // With preserved collinear vertices, coincident boundary edges cannot be
    // reconstructed from bounds alone. The engine owns their vertex retention.
    if (first == second || rectangle_strictly_contains(first, second)) {
        layout = rectangle_clip_solution_layout(first);
        return true;
    }
    if (rectangle_strictly_contains(second, first)) {
        layout = rectangle_clip_solution_layout(second);
        return true;
    }
    return false;
}
auto try_layout_two_rectangle_union(const Rect64& first,
                                    const Rect64& second,
                                    rectangle_path_layout& layout) -> bool {
    if (try_layout_rectangular_union(first, second, layout)) { return true; }
    if (!rectangles_have_strict_overlap(first, second)) { return false; }
    // Order by left edge once, then classify the two supported corner relations.
    const auto& left = first.left < second.left ? first : second;
    const auto& right = first.left < second.left ? second : first;
    if (left.left >= right.left || left.right >= right.right) { return false; }
    if (left.top < right.top && left.bottom < right.bottom) {
        layout = {left, right, descending_union_vertices};
    } else if (left.top > right.top && left.bottom > right.bottom) {
        layout = {left, right, ascending_union_vertices};
    } else {
        return false;
    }
    return true;
}
auto try_layout_rectangle_corner_difference(const Rect64& subject,
                                            const Rect64& clip,
                                            rectangle_path_layout& layout) -> bool {
    const auto intersection = strict_intersection_rect(subject, clip);
    if (!intersection) { return false; }
    if (*intersection == subject) {
        layout = {};
        return true;
    }
    const bool left = intersection->left == subject.left;
    const bool top = intersection->top == subject.top;
    const bool right = intersection->right == subject.right;
    const bool bottom = intersection->bottom == subject.bottom;
    if (static_cast<int>(left) + static_cast<int>(top) + static_cast<int>(right) +
            static_cast<int>(bottom) !=
        2) {
        return false;
    }
    std::span<const rectangle_vertex> vertices;
    if (right && bottom) {
        vertices = difference_bottom_right_vertices;
    } else if (right && top) {
        vertices = difference_top_right_vertices;
    } else if (left && top) {
        vertices = difference_top_left_vertices;
    } else if (left && bottom) {
        vertices = difference_bottom_left_vertices;
    } else {
        return false;
    }
    layout = {subject, *intersection, vertices};
    return true;
}
}  // namespace clipper2next::internal
