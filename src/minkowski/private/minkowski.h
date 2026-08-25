#pragma once

#include "clipper2next/core.h"

namespace clipper2next::internal {

[[nodiscard]] auto build_minkowski_quads(const Path64& pattern,
                                         const Path64& path,
                                         bool is_sum,
                                         bool is_closed) -> Paths64;

[[nodiscard]] auto union_minkowski_quads(Paths64&& subjects,
                                         FillRule fill_rule) -> Paths64;

}  // namespace clipper2next::internal
