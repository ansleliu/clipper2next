#pragma once

#include "clipper2next/core.h"

#include <optional>

namespace clipper2next::internal {

[[nodiscard]] auto rectangles_have_strict_overlap(const Rect64& first, const Rect64& second)
    -> bool;
[[nodiscard]] auto rectangles_share_edge_segment(const Rect64& first, const Rect64& second)
    -> bool;
[[nodiscard]] auto strict_intersection_rect(const Rect64& first, const Rect64& second)
    -> std::optional<Rect64>;
[[nodiscard]] auto rectangle_clip_solution_path(const Rect64& rect) -> Path64;
[[nodiscard]] auto try_build_two_rectangle_union(
    const Rect64& first, const Rect64& second, Path64& path) -> bool;
[[nodiscard]] auto try_build_rectangle_corner_difference(
    const Rect64& subject, const Rect64& clip, Path64& path) -> bool;

}  // namespace clipper2next::internal
