#pragma once

#include "clipper2next/geotypes/coordinate.hpp"

#include <cstdint>

namespace geotypes {

template <class T>
struct Point2 final {
    using coordinate_type = T;

    T x{};
    T y{};

    [[nodiscard]] constexpr auto operator*(double scale) const -> Point2 {
        return {coordinateCast<T>(x * scale), coordinateCast<T>(y * scale)};
    }

    [[nodiscard]] constexpr auto operator-() const -> Point2 {
        return {saturatedNegate(x), saturatedNegate(y)};
    }

    [[nodiscard]] constexpr auto operator+(const Point2& other) const -> Point2 {
        return {saturatedAdd(x, other.x), saturatedAdd(y, other.y)};
    }

    [[nodiscard]] constexpr auto operator-(const Point2& other) const -> Point2 {
        return {saturatedSubtract(x, other.x), saturatedSubtract(y, other.y)};
    }

    friend constexpr auto operator==(const Point2&, const Point2&) -> bool = default;
};

template <class Target, class Source>
[[nodiscard]] constexpr auto pointCast(
    const Point2<Source>& point,
    const CoordinateRounding rounding = CoordinateRounding::NearestEven)
    -> Point2<Target> {
    return {
        coordinateCast<Target>(point.x, rounding),
        coordinateCast<Target>(point.y, rounding)};
}

using Point2i32 = Point2<std::int32_t>;
using Point2i64 = Point2<std::int64_t>;
using Point2f = Point2<float>;
using Point2d = Point2<double>;

}  // namespace geotypes
