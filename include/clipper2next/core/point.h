#pragma once

#include "clipper2next/geotypes/point.hpp"

#include <cstdint>
#include <limits>

namespace clipper2next {

template <typename T>
using Point = geotypes::Point2<T>;

using Point64 = geotypes::Point2i64;
using PointD = geotypes::Point2d;
using point64 = Point64;
using pointd = PointD;

inline constexpr Point64 InvalidPoint64{
    (std::numeric_limits<std::int64_t>::max)(),
    (std::numeric_limits<std::int64_t>::max)()};
inline constexpr PointD InvalidPointD{
    (std::numeric_limits<double>::max)(),
    (std::numeric_limits<double>::max)()};

template <typename T>
[[nodiscard]] constexpr auto midpoint(const Point<T>& first,
                                      const Point<T>& second) -> Point<T> {
    return {geotypes::midpointCoordinate(first.x, second.x),
            geotypes::midpointCoordinate(first.y, second.y)};
}

template <typename T>
[[nodiscard]] constexpr auto negated(const Point<T>& point) -> Point<T> {
    return -point;
}

}  // namespace clipper2next
