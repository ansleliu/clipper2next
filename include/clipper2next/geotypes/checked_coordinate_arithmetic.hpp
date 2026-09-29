#pragma once

#include "clipper2next/geotypes/checked_coordinate_cast.hpp"

namespace geotypes {

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedAdd(T left, T right) -> std::expected<T, CoordinateError> {
    constexpr auto low = (std::numeric_limits<T>::lowest)();
    constexpr auto high = (std::numeric_limits<T>::max)();
    if constexpr (std::is_unsigned_v<T>) {
        if (right > high - left) { return std::unexpected(CoordinateError::OutOfRange); }
    } else {
        if ((right > 0 && left > high - right) || (right < 0 && left < low - right)) {
            return std::unexpected(CoordinateError::OutOfRange);
        }
    }
    return static_cast<T>(left + right);
}

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedSubtract(T left, T right) -> std::expected<T, CoordinateError> {
    constexpr auto low = (std::numeric_limits<T>::lowest)();
    constexpr auto high = (std::numeric_limits<T>::max)();
    if constexpr (std::is_unsigned_v<T>) {
        if (right > left) { return std::unexpected(CoordinateError::OutOfRange); }
    } else {
        if ((right > 0 && left < low + right) || (right < 0 && left > high + right)) {
            return std::unexpected(CoordinateError::OutOfRange);
        }
    }
    return static_cast<T>(left - right);
}

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedNegate(T value) -> std::expected<T, CoordinateError> {
    return checkedSubtract(T{}, value);
}

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedMultiply(T left, T right) -> std::expected<T, CoordinateError> {
    constexpr auto low = (std::numeric_limits<T>::lowest)();
    constexpr auto high = (std::numeric_limits<T>::max)();
    if (left == 0 || right == 0) { return T{}; }
    if constexpr (std::is_unsigned_v<T>) {
        if (left > high / right) { return std::unexpected(CoordinateError::OutOfRange); }
    } else {
        const auto overflow = left > 0
            ? (right > 0 ? left > high / right : right < low / left)
            : (right > 0 ? left < low / right : left < high / right);
        if (overflow) { return std::unexpected(CoordinateError::OutOfRange); }
    }
    return static_cast<T>(left * right);
}

namespace detail {

template <typename T, typename Operation>
[[nodiscard]] constexpr auto checkedPointOperation(
    const Point2<T>& left, const Point2<T>& right, Operation operation)
    -> std::expected<Point2<T>, CoordinateError> {
    const auto x = operation(left.x, right.x);
    if (!x) { return std::unexpected(x.error()); }
    const auto y = operation(left.y, right.y);
    if (!y) { return std::unexpected(y.error()); }
    return Point2<T>{*x, *y};
}

}  // namespace detail

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedAdd(const Point2<T>& left, const Point2<T>& right)
    -> std::expected<Point2<T>, CoordinateError> {
    return detail::checkedPointOperation(left, right, [](T a, T b) { return checkedAdd(a, b); });
}

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedSubtract(const Point2<T>& left, const Point2<T>& right)
    -> std::expected<Point2<T>, CoordinateError> {
    return detail::checkedPointOperation(left, right, [](T a, T b) { return checkedSubtract(a, b); });
}

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedNegate(const Point2<T>& value)
    -> std::expected<Point2<T>, CoordinateError> {
    return checkedSubtract(Point2<T>{}, value);
}

template <typename T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
[[nodiscard]] constexpr auto checkedMultiply(const Point2<T>& value, T factor)
    -> std::expected<Point2<T>, CoordinateError> {
    return detail::checkedPointOperation(value, Point2<T>{factor, factor},
        [](T a, T b) { return checkedMultiply(a, b); });
}

}  // namespace geotypes
