#pragma once

#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

[[nodiscard]] auto repair_triangulation_graph(triangulation_context& context) -> bool;

auto merge_duplicate_or_collinear_vertices(triangulation_context& context) -> void;

}  // namespace clipper2next::internal
