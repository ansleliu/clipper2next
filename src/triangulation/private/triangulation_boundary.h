#pragma once

#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

[[nodiscard]] auto build_triangulation_boundary(triangulation_context& context,
                                                const Paths64& paths) -> bool;

}  // namespace clipper2next::internal
