#pragma once

#include "clipper2next/geotypes/point.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace geotypes {

enum class CoordinateError {
    NonFinite,
    OutOfRange,
    InvalidRounding,
};

// Checks representable range, not losslessness or an algorithm's working domain.
// Floating-to-integral conversion checks the rounded value before casting.
template <typename Target, typename Source>
    requires (std::is_arithmetic_v<Target> && std::is_arithmetic_v<Source> &&
              !std::is_same_v<Target, bool> && !std::is_same_v<Source, bool>)
[[nodiscard]] inline auto checkedCoordinateCast(
    Source value, CoordinateRounding rounding = CoordinateRounding::NearestEven)
    -> std::expected<Target, CoordinateError> {
    if (rounding != CoordinateRounding::NearestEven &&
        rounding != CoordinateRounding::NearestAwayFromZero) {
        return std::unexpected(CoordinateError::InvalidRounding);
    }
    if constexpr (std::is_floating_point_v<Source>) {
        if (!std::isfinite(value)) { return std::unexpected(CoordinateError::NonFinite); }
    }
    if constexpr (std::is_integral_v<Target> && std::is_floating_point_v<Source>) {
        const auto rounded = roundToNearest(value, rounding);
        // A power-of-two exclusive upper bound is exact even when long double
        // has only 53 significand bits. In particular, +2^63 is not INT64_MAX.
        const auto upper = std::ldexp(Source{1}, std::numeric_limits<Target>::digits);
        const auto lower = std::is_signed_v<Target> ? -upper : Source{};
        if (rounded < lower || rounded >= upper) {
            return std::unexpected(CoordinateError::OutOfRange);
        }
        return static_cast<Target>(rounded);
    } else if constexpr (std::is_integral_v<Target> && std::is_integral_v<Source>) {
        if constexpr (std::is_signed_v<Source>) {
            if (value < 0) {
                if constexpr (std::is_unsigned_v<Target>) {
                    return std::unexpected(CoordinateError::OutOfRange);
                } else {
                    if (static_cast<std::intmax_t>(value) <
                        static_cast<std::intmax_t>((std::numeric_limits<Target>::lowest)())) {
                        return std::unexpected(CoordinateError::OutOfRange);
                    }
                    return static_cast<Target>(value);
                }
            }
        }
        if (static_cast<std::uintmax_t>(value) >
            static_cast<std::uintmax_t>((std::numeric_limits<Target>::max)())) {
            return std::unexpected(CoordinateError::OutOfRange);
        }
        return static_cast<Target>(value);
    } else if constexpr (std::is_floating_point_v<Source> &&
                         std::numeric_limits<Target>::max_exponent <
                             std::numeric_limits<Source>::max_exponent) {
        const auto maximum = static_cast<Source>((std::numeric_limits<Target>::max)());
        const auto bounded = std::clamp(value, -maximum, maximum);
        // Reject out-of-range values rather than returning a saturated value.
        // The conversion expression itself stays bounded even if an optimizer
        // folds a constant cast before discarding an unreachable branch.
        if (bounded != value) { return std::unexpected(CoordinateError::OutOfRange); }
        return static_cast<Target>(bounded);
    } else {
        const auto wide = static_cast<long double>(value);
        const auto maximum = static_cast<long double>((std::numeric_limits<Target>::max)());
        if (wide < -maximum || wide > maximum) {
            return std::unexpected(CoordinateError::OutOfRange);
        }
        return static_cast<Target>(value);
    }
}

template <typename Target, typename Source>
[[nodiscard]] inline auto checkedPointCast(
    const Point2<Source>& value, CoordinateRounding rounding = CoordinateRounding::NearestEven)
    -> std::expected<Point2<Target>, CoordinateError> {
    const auto x = checkedCoordinateCast<Target>(value.x, rounding);
    if (!x) { return std::unexpected(x.error()); }
    const auto y = checkedCoordinateCast<Target>(value.y, rounding);
    if (!y) { return std::unexpected(y.error()); }
    return Point2<Target>{*x, *y};
}

}  // namespace geotypes
