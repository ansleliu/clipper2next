#pragma once

#include "triangulation/private/triangulation_context.h"
#include "clipper2next/triangulation/request.h"

namespace clipper2next::internal {

auto add_sweep_active_edge(triangulation_context& context, triangulation_edge* edge) -> void;

auto remove_sweep_active_edge(triangulation_context& context, triangulation_edge* edge) -> void;

[[nodiscard]] auto create_sweep_loose_edge(triangulation_context& context,
                                           triangulation_vertex* first,
                                           triangulation_vertex* second) -> triangulation_edge*;

[[nodiscard]] auto create_sweep_triangle(triangulation_context& context,
                                         triangulation_edge* first,
                                         triangulation_edge* second,
                                         triangulation_edge* third) -> triangulation_triangle*;

[[nodiscard]] auto run_triangulation_sweep(triangulation_context& context) -> TriangulateResult;

}  // namespace clipper2next::internal
