#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/core.h"

namespace clipper2next {

[[nodiscard]] CLIPPER2NEXT_API auto trim_collinear(
    const Path64& path, bool is_open_path = false) -> Path64;
[[nodiscard]] CLIPPER2NEXT_API auto simplify_path(
    const Path64& path, double epsilon, bool is_closed_path = true)
    -> Path64;
[[nodiscard]] CLIPPER2NEXT_API auto simplify_paths(
    const Paths64& paths, double epsilon, bool is_closed_path = true)
    -> Paths64;
[[nodiscard]] CLIPPER2NEXT_API auto ramer_douglas_peucker(
    const Path64& path, double epsilon) -> Path64;
[[nodiscard]] CLIPPER2NEXT_API auto ramer_douglas_peucker(
    const Paths64& paths, double epsilon) -> Paths64;
[[nodiscard]] CLIPPER2NEXT_API auto make_ellipse(
    const Point64& center,
    double radius_x,
    double radius_y = 0.0,
    std::size_t steps = 0) -> Path64;
[[nodiscard]] CLIPPER2NEXT_API auto make_ellipse(
    const Rect64& rect, std::size_t steps = 0) -> Path64;
[[nodiscard]] CLIPPER2NEXT_API auto path_contains_path(
    const Path64& inner, const Path64& outer) -> bool;

}  // namespace clipper2next
