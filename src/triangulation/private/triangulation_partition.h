#pragma once
#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {
// Consumes a successfully prepared boundary before any triangulation. Endpoint
// events and closed-contour incidence invariants belong to that producer.
[[nodiscard]] auto try_build_partitioned_triangulation_graph(
    triangulation_context& context, const triangulation_predicate_domain& domain) -> bool;
// Prepares ordered corner references for a Sweep result inside the operation lease.
[[nodiscard]] auto try_prepare_partitioned_sweep(
    triangulation_context& context, const triangulation_predicate_domain& domain) -> bool;
}
