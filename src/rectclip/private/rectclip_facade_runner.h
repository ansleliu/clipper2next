#pragma once

#include "clipper2next/core.h"

#include <span>

namespace clipper2next::internal {

enum class rectclip_mode { polygons, lines };

struct rectclip_context;

// Existing workspace callers and the frozen alias/fault audit share this dispatcher.
[[nodiscard]] auto build_rectclip_paths(
    rectclip_context&, const Paths64&, std::span<const Rect64>, rectclip_mode) -> Paths64;

// Lines own a rectangle value and point sequences; they do not use polygon storage.
[[nodiscard]] auto build_rectclip_line_paths(
    Rect64, const Paths64&, std::span<const Rect64>) -> Paths64;

// Unchecked raw polygons need only the bounds of the path being consumed.
[[nodiscard]] auto build_rectclip_unprepared_polygon_paths(
    rectclip_context&, const Paths64&) -> Paths64;

auto execute_rectclip_into(
    rectclip_context&, const Paths64&, std::span<const Rect64>, rectclip_mode, Paths64&) -> void;
auto execute_rectclip_lines_into(
    Rect64, const Paths64&, std::span<const Rect64>, Paths64&) -> void;

}  // namespace clipper2next::internal
