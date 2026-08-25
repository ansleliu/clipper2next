#pragma once

#include "rectclip/private/rectclip_graph.h"

namespace clipper2next::internal {

[[nodiscard]] auto adjacent_location(rect_location location, bool clockwise) noexcept
    -> rect_location;

[[nodiscard]] auto heading_clockwise(rect_location previous, rect_location current) noexcept
    -> bool;

[[nodiscard]] auto are_opposites(rect_location previous, rect_location current) noexcept -> bool;

[[nodiscard]] auto is_clockwise(rect_location previous,
                                rect_location current,
                                const Point64& previous_point,
                                const Point64& current_point,
                                const Point64& rect_midpoint) -> bool;

[[nodiscard]] auto start_locations_are_clockwise(const std::vector<rect_location>& start_locations)
    -> bool;

[[nodiscard]] auto next_external_location(const Rect64& rect,
                                          const Path64& path,
                                          rect_location location,
                                          size_t& index,
                                          size_t high_index) -> rect_location;

}  // namespace clipper2next::internal
