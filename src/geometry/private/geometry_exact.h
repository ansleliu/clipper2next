#pragma once

#include "clipper2next/geometry/math.h"

#include <cstdint>

namespace clipper2next::internal::geometry_exact {

[[nodiscard]] inline auto unsigned_product(uint64_t first, uint64_t second) -> UInt128Struct {
    return multiply_uint64(first, second);
}

[[nodiscard]] inline auto product_sign(int64_t first, int64_t second) -> int {
    return tri_sign(first) * tri_sign(second);
}

[[nodiscard]] inline auto compare_unsigned_product(const UInt128Struct& first,
                                                   const UInt128Struct& second) -> int {
    if (first.hi != second.hi) { return first.hi > second.hi ? 1 : -1; }
    if (first.lo == second.lo) { return 0; }
    return first.lo > second.lo ? 1 : -1;
}

}  // namespace clipper2next::internal::geometry_exact
