#pragma once
#include "triangulation/private/triangulation_mesh.h"

namespace clipper2next::internal {
auto build_triangulation_result_into(const triangulation_mesh_view&, Paths64&) -> void;
auto build_triangulation_result_into(const triangulation_mesh_view&, PathsD&, double) -> void;
[[nodiscard]] auto build_triangulation_result(const triangulation_mesh_view&) -> Paths64;
}  // namespace clipper2next::internal
