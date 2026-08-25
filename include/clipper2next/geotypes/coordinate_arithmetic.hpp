#pragma once

#include "clipper2next/geotypes/coordinate_cast.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace geotypes {

template <typename T>
[[nodiscard]] constexpr auto saturatedAdd(T left, T right) noexcept -> T {
    if constexpr (!std::is_integral_v<T>) {
        return left + right;
    } else if constexpr (std::is_unsigned_v<T>) {
        const auto maximum = (std::numeric_limits<T>::max)();
        return right > maximum - left ? maximum : static_cast<T>(left + right);
    } else {
        const auto minimum = (std::numeric_limits<T>::lowest)();
        const auto maximum = (std::numeric_limits<T>::max)();
        if (right > 0 && left > maximum - right) { return maximum; }
        if (right < 0 && left < minimum - right) { return minimum; }
        return static_cast<T>(left + right);
    }
}

template <typename T>
[[nodiscard]] constexpr auto saturatedSubtract(T left, T right) noexcept -> T {
    if constexpr (!std::is_integral_v<T>) {
        return left - right;
    } else if constexpr (std::is_unsigned_v<T>) {
        return right > left ? T{} : static_cast<T>(left - right);
    } else {
        const auto minimum = (std::numeric_limits<T>::lowest)();
        const auto maximum = (std::numeric_limits<T>::max)();
        if (right > 0 && left < minimum + right) { return minimum; }
        if (right < 0 && left > maximum + right) { return maximum; }
        return static_cast<T>(left - right);
    }
}

template <typename T>
[[nodiscard]] constexpr auto saturatedNegate(T value) noexcept -> T {
    if constexpr (!std::is_integral_v<T>) {
        return -value;
    } else if constexpr (std::is_unsigned_v<T>) {
        return T{};
    } else {
        return value == (std::numeric_limits<T>::lowest)()
                   ? (std::numeric_limits<T>::max)()
                   : static_cast<T>(-value);
    }
}

template <typename T>
[[nodiscard]] constexpr auto saturatedReflect(T value, T pivot) noexcept -> T {
    if constexpr (!std::is_integral_v<T>) {
        return pivot + (pivot - value);
    } else if constexpr (std::is_unsigned_v<T>) {
        if (pivot >= value) {
            return saturatedAdd(pivot, static_cast<T>(pivot - value));
        }
        return saturatedSubtract(pivot, static_cast<T>(value - pivot));
    } else {
        return saturatedAdd(pivot, saturatedSubtract(pivot, value));
    }
}

template <typename T>
[[nodiscard]] constexpr auto signedMagnitude(T value) noexcept -> std::uintmax_t {
    static_assert(std::is_integral_v<T> && std::is_signed_v<T>);
    return value < 0
               ? static_cast<std::uintmax_t>(-(value + T{1})) + std::uintmax_t{1}
               : static_cast<std::uintmax_t>(value);
}

template <typename T, typename Delta>
[[nodiscard]] constexpr auto saturatedAddIntegralDelta(T value, Delta delta) noexcept -> T {
    static_assert(std::is_integral_v<T> && std::is_integral_v<Delta>);
    const bool delta_is_negative = std::is_signed_v<Delta> && delta < 0;
    const std::uintmax_t magnitude = [&]() constexpr {
        if constexpr (std::is_signed_v<Delta>) {
            return signedMagnitude(delta);
        } else {
            return static_cast<std::uintmax_t>(delta);
        }
    }();

    if constexpr (std::is_unsigned_v<T>) {
        if (!delta_is_negative) {
            const auto room = static_cast<std::uintmax_t>((std::numeric_limits<T>::max)() - value);
            return magnitude > room ? (std::numeric_limits<T>::max)()
                                    : static_cast<T>(value + static_cast<T>(magnitude));
        }
        return magnitude > static_cast<std::uintmax_t>(value)
                   ? T{}
                   : static_cast<T>(value - static_cast<T>(magnitude));
    } else {
        constexpr auto minimum = (std::numeric_limits<T>::lowest)();
        constexpr auto maximum = (std::numeric_limits<T>::max)();
        constexpr std::uintmax_t minimum_magnitude = signedMagnitude(minimum);
        const auto from_negative_magnitude = [](std::uintmax_t amount) {
            if (amount == minimum_magnitude) { return minimum; }
            return static_cast<T>(-static_cast<T>(amount));
        };

        if (!delta_is_negative) {
            if (value < 0) {
                const auto value_magnitude = signedMagnitude(value);
                if (magnitude < value_magnitude) {
                    return from_negative_magnitude(value_magnitude - magnitude);
                }
                const auto positive_result = magnitude - value_magnitude;
                return positive_result > static_cast<std::uintmax_t>(maximum)
                           ? maximum
                           : static_cast<T>(positive_result);
            }
            const auto room = static_cast<std::uintmax_t>(maximum - value);
            return magnitude > room ? maximum
                                    : static_cast<T>(value + static_cast<T>(magnitude));
        }

        if (value > 0) {
            const auto positive_value = static_cast<std::uintmax_t>(value);
            if (magnitude <= positive_value) {
                return static_cast<T>(positive_value - magnitude);
            }
            const auto negative_result = magnitude - positive_value;
            return negative_result > minimum_magnitude
                       ? minimum
                       : from_negative_magnitude(negative_result);
        }
        const auto value_magnitude = signedMagnitude(value);
        const auto room = minimum_magnitude - value_magnitude;
        return magnitude > room ? minimum
                                : from_negative_magnitude(value_magnitude + magnitude);
    }
}

template <typename T>
[[nodiscard]] constexpr auto saturatedAddSignedDelta(T value, std::int64_t delta) noexcept -> T {
    static_assert(std::is_integral_v<T> && sizeof(T) <= sizeof(std::int64_t));
    return saturatedAddIntegralDelta(value, delta);
}

template <typename T, typename Delta>
[[nodiscard]] inline auto saturatedAddTruncatingDelta(T value, Delta delta) -> T {
    if constexpr (!std::is_integral_v<T>) {
        return saturatedAdd(value, truncatingCoordinateCast<T>(delta));
    } else if constexpr (std::is_integral_v<Delta>) {
        return saturatedAddIntegralDelta(value, delta);
    } else if constexpr (std::is_floating_point_v<Delta>) {
        if (std::isnan(delta)) { return value; }
        const long double combined =
            static_cast<long double>(value) + std::trunc(static_cast<long double>(delta));
        const auto minimum = static_cast<long double>((std::numeric_limits<T>::lowest)());
        const auto maximum = static_cast<long double>((std::numeric_limits<T>::max)());
        if (combined <= minimum) { return (std::numeric_limits<T>::lowest)(); }
        if (combined >= maximum) { return (std::numeric_limits<T>::max)(); }
        return static_cast<T>(combined);
    } else {
        return saturatedAdd(value, truncatingCoordinateCast<T>(delta));
    }
}

}  // namespace geotypes
