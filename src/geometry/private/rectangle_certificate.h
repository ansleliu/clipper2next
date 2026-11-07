#pragma once

#include "clipper2next/core/point.h"
#include "clipper2next/core/rect.h"

#include <optional>
#include <span>

namespace clipper2next::internal {

struct rectangle_certificate final {
    Rect64 bounds;
    bool positive;
};

// Nonzero alternating axis edges establish four distinct bounding corners and
// their cyclic order. Comparisons alone retain the full integer coordinate domain.
[[nodiscard]] inline auto classify_axis_aligned_rectangle(std::span<const Point64> path)
    -> std::optional<rectangle_certificate> {
    if (path.size() != 4U) { return std::nullopt; }
    const auto& first = path[0];
    const auto& second = path[1];
    const auto& opposite = path[2];
    const auto& fourth = path[3];
    const auto horizontal_first = first.y == second.y;
    if (horizontal_first) {
        if (first.x == second.x || second.x != opposite.x || second.y == opposite.y ||
            opposite.y != fourth.y || fourth.x != first.x) { return std::nullopt; }
    } else {
        if (first.x != second.x || second.y != opposite.y || second.x == opposite.x ||
            opposite.x != fourth.x || fourth.y != first.y) { return std::nullopt; }
    }
    const auto rightward = first.x < opposite.x;
    const auto downward = first.y < opposite.y;
    return rectangle_certificate{
        {rightward ? first.x : opposite.x, downward ? first.y : opposite.y,
         rightward ? opposite.x : first.x, downward ? opposite.y : first.y},
        horizontal_first ? rightward == downward : rightward != downward};
}

[[nodiscard]] inline auto rectangle_strictly_contains(const Rect64& outer,
                                                     const Rect64& inner) noexcept -> bool {
    return outer.left < inner.left && outer.top < inner.top &&
           outer.right > inner.right && outer.bottom > inner.bottom;
}

}  // namespace clipper2next::internal
