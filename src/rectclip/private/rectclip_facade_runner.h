#pragma once

#include "clipper2next/core.h"
#include "rectclip/private/rectclip_path_bounds.h"

#include <span>

namespace clipper2next::internal {

enum class rectclip_mode {
    polygons,
    lines,
};

[[nodiscard]] auto execute_rectclip(
    const Rect64& rect,
    const Paths64& paths,
    rectclip_mode mode)
    -> Paths64;

[[nodiscard]] auto execute_rectclip(const Rect64& rect,
                                    const Paths64& paths,
                                    std::span<const Rect64> path_bounds,
                                    rectclip_mode mode) -> Paths64;

[[nodiscard]] auto execute_rectclip(const Rect64& rect,
                                    const Paths64& paths,
                                    const rectclip_path_bounds_view& path_bounds,
                                    rectclip_mode mode) -> Paths64;

[[nodiscard]] auto copy_rectclip_paths(const Paths64& paths) -> Paths64;

}  // namespace clipper2next::internal
