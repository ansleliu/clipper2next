#pragma once

#include "triangulation/private/triangulation_sweep_diagonal.h"
#include "clipper2next/triangulation/request.h"

namespace clipper2next::internal {

auto add_sweep_active_edge(triangulation_context& context, triangulation_edge* edge) -> void;

auto remove_sweep_active_edge(triangulation_context& context, triangulation_edge* edge) -> void;

[[nodiscard]] auto create_sweep_loose_edge(triangulation_sweep_diagonal&& proof) -> triangulation_edge*;

[[nodiscard]] auto create_sweep_triangle(triangulation_context& context,
                                         triangulation_edge* first,
                                         triangulation_edge* second,
                                         triangulation_edge* third,
                                          const triangulation_predicate_domain& domain) -> triangulation_triangle*;

// Selection is read-only; a successful proof is consumed once by the materializer.
[[nodiscard]] auto nearest_sweep_diagonal(const triangulation_sweep_front& front, std::size_t event_id)
    -> std::optional<triangulation_sweep_diagonal>;

[[nodiscard]] auto run_triangulation_sweep(triangulation_context& context,
                                             const triangulation_predicate_domain& domain) -> TriangulateResult;

}  // namespace clipper2next::internal
