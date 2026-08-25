// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/geotypes/coordinate.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace clipper2next {

inline constexpr int64_t MAX_COORD = INT64_MAX >> 2;
inline constexpr int64_t MIN_COORD = -MAX_COORD;
inline constexpr int64_t INVALID = INT64_MAX;
inline constexpr double max_coord = static_cast<double>(MAX_COORD);
inline constexpr double min_coord = static_cast<double>(MIN_COORD);
inline constexpr double MAX_DBL = (std::numeric_limits<double>::max)();
inline constexpr int decimal_precision_limit = 8;
inline constexpr double coordinate_rounding_guard = 4096.0;

struct scale_request {
    double x{1.0};
    double y{1.0};
};

[[nodiscard]] inline auto check_precision_range(int precision) -> clipper_result<int> {
    if (precision < -decimal_precision_limit || precision > decimal_precision_limit) {
        return make_clipper_error<int>(clipper_error_code::precision_out_of_range);
    }
    return precision;
}

template <typename Source>
[[nodiscard]] inline auto scaled_coordinate_fits(Source coordinate, double scale) -> bool {
    if constexpr (std::is_integral_v<Source>) {
        if (scale == 1.0) { return coordinate >= MIN_COORD && coordinate <= MAX_COORD; }
    }

    const auto scaled = static_cast<double>(coordinate) * scale;
    if (!std::isfinite(scaled)) { return false; }
    const auto guarded_min = static_cast<double>(MIN_COORD) + coordinate_rounding_guard;
    const auto guarded_max = static_cast<double>(MAX_COORD) - coordinate_rounding_guard;
    const auto rounded = geotypes::roundToEven(scaled);
    return rounded >= guarded_min && rounded <= guarded_max;
}

}  // namespace clipper2next
