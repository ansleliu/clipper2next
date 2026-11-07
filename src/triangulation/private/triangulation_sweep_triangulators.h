#pragma once

#include "triangulation/private/triangulation_sweep_front.h"

namespace clipper2next::internal {

auto triangulate_left(const triangulation_sweep_front& front,
                      triangulation_edge* edge,
                      triangulation_vertex* pivot) -> void;

auto triangulate_right(const triangulation_sweep_front& front,
                       triangulation_edge* edge,
                       triangulation_vertex* pivot) -> void;

}  // namespace clipper2next::internal
