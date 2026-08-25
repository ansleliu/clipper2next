#pragma once

#include "triangulation/private/triangulation_sweep_line.h"

namespace clipper2next::internal {

auto add_triangulation_active_edge(triangulation_context& context, triangulation_edge* edge)
    -> void;

auto remove_triangulation_active_edge(triangulation_context& context, triangulation_edge* edge)
    -> void;

}  // namespace clipper2next::internal
