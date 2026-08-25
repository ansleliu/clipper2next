#pragma once

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace geotypes {

enum class CoordinateRounding {
    NearestEven,
    NearestAwayFromZero,
};

template <typename Value>
[[nodiscard]] inline auto roundToEven(Value value) noexcept -> Value {
    static_assert(std::is_floating_point_v<Value>);
    if (!std::isfinite(value)) { return value; }
    const auto lower = std::floor(value);
    const auto fraction = value - lower;
    if (fraction != Value{0.5}) {
        return fraction < Value{0.5} ? lower : lower + Value{1};
    }
    return std::fmod(std::fabs(lower), Value{2}) == Value{} ? lower : lower + Value{1};
}

template <typename Value>
[[nodiscard]] inline auto roundToNearest(
    Value value,
    const CoordinateRounding rounding) noexcept -> Value {
    static_assert(std::is_floating_point_v<Value>);
    if (rounding == CoordinateRounding::NearestAwayFromZero) {
        return std::round(value);
    }
    return roundToEven(value);
}

template <typename Target, typename Source>
[[nodiscard]] inline auto coordinateCast(
    Source value,
    const CoordinateRounding rounding = CoordinateRounding::NearestEven)
    -> Target {
    if constexpr (std::is_same_v<std::remove_cv_t<Target>, std::remove_cv_t<Source>>) {
        return value;
    } else if constexpr (std::is_integral_v<Target> && std::is_integral_v<Source>) {
        if (std::in_range<Target>(value)) { return static_cast<Target>(value); }
        if constexpr (std::is_signed_v<Source>) {
            if (value < 0) { return (std::numeric_limits<Target>::lowest)(); }
        }
        return (std::numeric_limits<Target>::max)();
    } else if constexpr (std::is_integral_v<Target> && std::is_floating_point_v<Source>) {
        const auto rounded = roundToNearest(value, rounding);
        if (std::isnan(rounded)) { return Target{}; }
        const auto wide = static_cast<long double>(rounded);
        const auto minimum = static_cast<long double>((std::numeric_limits<Target>::lowest)());
        const auto maximum = static_cast<long double>((std::numeric_limits<Target>::max)());
        if (wide <= minimum) { return (std::numeric_limits<Target>::lowest)(); }
        if (wide >= maximum) { return (std::numeric_limits<Target>::max)(); }
        return static_cast<Target>(rounded);
    } else {
        return static_cast<Target>(value);
    }
}

template <typename Target, typename Source>
[[nodiscard]] inline auto truncatingCoordinateCast(Source value) -> Target {
    if constexpr (std::is_integral_v<Target> && std::is_integral_v<Source>) {
        return coordinateCast<Target>(value);
    } else if constexpr (std::is_integral_v<Target> && !std::is_integral_v<Source>) {
        if (std::isnan(value)) { return Target{}; }
        const auto wide = static_cast<long double>(value);
        const auto minimum = static_cast<long double>((std::numeric_limits<Target>::lowest)());
        const auto maximum = static_cast<long double>((std::numeric_limits<Target>::max)());
        if (wide <= minimum) { return (std::numeric_limits<Target>::lowest)(); }
        if (wide >= maximum) { return (std::numeric_limits<Target>::max)(); }
        return static_cast<Target>(value);
    } else {
        return static_cast<Target>(value);
    }
}

}  // namespace geotypes
