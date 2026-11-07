#pragma once
#include "clipper2next/triangulation/request.h"

#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

[[nodiscard]] auto build_triangulation_boundary(triangulation_context& context,
                                                const Paths64& paths) -> TriangulateResult;

}  // namespace clipper2next::internal
