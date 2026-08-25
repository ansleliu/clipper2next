#pragma once

#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

auto triangulate_left(triangulation_context& context,
                      triangulation_edge* edge,
                      triangulation_vertex* pivot,
                      int64_t minimum_y) -> void;

auto triangulate_right(triangulation_context& context,
                       triangulation_edge* edge,
                       triangulation_vertex* pivot,
                       int64_t minimum_y) -> void;

}  // namespace clipper2next::internal
