#pragma once

#include "triangulation/private/triangulation_types.h"

namespace clipper2next::internal {

[[nodiscard]] auto ShortestDistFromSegment(const Point64& point,
                                           const Point64& segment_start,
                                           const Point64& segment_end) -> double;

[[nodiscard]] auto SegsIntersect(Point64 segment1_start,
                                 Point64 segment1_end,
                                 Point64 segment2_start,
                                 Point64 segment2_end) -> triangulation_intersect_kind;

}  // namespace clipper2next::internal
