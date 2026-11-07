#pragma once

#include "clipper2next/triangulation/request.h"
#include "triangulation/private/triangulation_mesh.h"

namespace clipper2next::internal {
struct triangulation_context;
class triangulation_operation_lease;
auto execute_triangulation_graph(triangulation_operation_lease&, const Paths64&, bool, TriangulateResult&)
    -> triangulation_mesh_view;

auto execute_sweep_triangulation_into(triangulation_operation_lease&, const Paths64&, TriangulateResult&, Paths64&)
    -> void;
auto execute_sweep_triangulation_into(triangulation_operation_lease&, const Paths64&, TriangulateResult&, PathsD&, double)
    -> void;

[[nodiscard]] auto execute_triangulation(const Paths64& paths,
                                         bool use_delaunay,
                                         TriangulateResult& result) -> Paths64;


}  // namespace clipper2next::internal
