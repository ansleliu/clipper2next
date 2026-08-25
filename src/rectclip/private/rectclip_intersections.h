#pragma once

#include "rectclip/private/rectclip_graph.h"

namespace clipper2next::internal {

[[nodiscard]] auto is_horizontal(const Point64& first, const Point64& second) noexcept -> bool;

[[nodiscard]] auto get_segment_intersection(const Point64& first_start,
                                            const Point64& first_end,
                                            const Point64& second_start,
                                            const Point64& second_end,
                                            Point64& intersection) -> bool;

[[nodiscard]] auto get_intersection(const Rect64& rect,
                                    const Point64& first,
                                    const Point64& second,
                                    rect_location& location,
                                    Point64& intersection) -> bool;

}  // namespace clipper2next::internal
