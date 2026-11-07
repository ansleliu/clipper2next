#pragma once

#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_exact_predicates.h"

namespace clipper2next::internal {



auto force_triangulation_edge_legal(triangulation_context& context, triangulation_edge* edge,
                                     const triangulation_predicate_domain& domain)
    -> bool;

auto legalize_pending_delaunay_edges(triangulation_context& context,
                                      const triangulation_predicate_domain& domain) -> bool;

}  // namespace clipper2next::internal
