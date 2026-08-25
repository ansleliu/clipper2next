#pragma once

#include "triangulation/private/triangulation_types.h"

namespace clipper2next::internal {

auto make_triangulation_edge(triangulation_vertex* first,
                             triangulation_vertex* second,
                             triangulation_edge_kind kind = triangulation_edge_kind::loose)
    -> triangulation_edge;

void initialize_triangulation_edge(triangulation_edge& edge,
                                   triangulation_vertex* first,
                                   triangulation_vertex* second,
                                   triangulation_edge_kind kind);

}  // namespace clipper2next::internal
