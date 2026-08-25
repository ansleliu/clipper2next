#pragma once

#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

[[nodiscard]] auto build_triangulation_result(const triangulation_context& context)
    -> Paths64;

}  // namespace clipper2next::internal
