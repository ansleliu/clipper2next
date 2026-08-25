#pragma once

#include "rectclip/private/rectclip_graph.h"

namespace clipper2next::internal {

[[nodiscard]] auto classify_point(const Rect64& rect, const Point64& point) -> rect_location;

[[nodiscard]] auto is_on_rect_boundary(const Rect64& rect, const Point64& point) -> bool;

}  // namespace clipper2next::internal
