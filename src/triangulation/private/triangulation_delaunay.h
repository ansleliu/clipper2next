#pragma once

#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

auto force_triangulation_edge_legal(triangulation_context& context, triangulation_edge* edge)
    -> bool;

auto legalize_pending_delaunay_edges(triangulation_context& context) -> bool;

}  // namespace clipper2next::internal
