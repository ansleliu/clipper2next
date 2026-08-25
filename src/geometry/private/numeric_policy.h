#pragma once

#include "clipper2next/geometry/scale.h"

#include <algorithm>
#include <numbers>

namespace clipper2next::internal {

inline constexpr auto pi = std::numbers::pi_v<double>;
inline constexpr auto precision_limit = decimal_precision_limit;

[[nodiscard]] constexpr auto is_decimal_precision_in_range(int precision) noexcept -> bool {
    return precision >= -precision_limit && precision <= precision_limit;
}

[[nodiscard]] constexpr auto clamp_decimal_precision(int precision) noexcept -> int {
    return std::clamp(precision, -precision_limit, precision_limit);
}

}  // namespace clipper2next::internal
