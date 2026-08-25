#pragma once

#include "clipper2next/core.h"

#include <span>
#include <vector>

namespace clipper2next::internal {

enum class rectclip_mode;

struct rectclip_path_bounds_summary final {
    Rect64 combined_bounds = Rect64::invalid_rect();
    bool has_bounds = false;
    bool all_paths_have_polygon_minimum_size = true;
    bool all_paths_have_line_minimum_size = true;
};

struct rectclip_path_bounds_view final {
    std::span<const Rect64> bounds{};
    rectclip_path_bounds_summary summary{};
};

[[nodiscard]] auto rectclip_rect_in_range(const Rect64& rect) -> bool;

[[nodiscard]] auto build_rectclip_path_bounds_if_in_range(const Paths64& paths,
                                                          std::vector<Rect64>& path_bounds)
    -> bool;

[[nodiscard]] auto build_rectclip_path_bounds_if_in_range(
    const Paths64& paths,
    std::vector<Rect64>& path_bounds,
    rectclip_path_bounds_summary& summary) -> bool;

[[nodiscard]] auto summarize_rectclip_path_bounds(
    const Paths64& paths,
    std::span<const Rect64> path_bounds) -> rectclip_path_bounds_summary;

[[nodiscard]] auto rectclip_paths_have_minimum_size(
    const Paths64& paths, std::size_t minimum_size) -> bool;
[[nodiscard]] auto rectclip_has_precomputed_bounds(
    const Paths64& paths, std::span<const Rect64> path_bounds) -> bool;
[[nodiscard]] auto rectclip_paths_bounds_unchecked(
    const Paths64& paths, Rect64& combined_bounds) -> bool;
[[nodiscard]] auto rectclip_rect_contains_bounds(
    const Rect64& rect, const Rect64& path_bounds, rectclip_mode mode) -> bool;

}  // namespace clipper2next::internal
