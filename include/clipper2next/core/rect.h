#pragma once

#include "clipper2next/core/path.h"
#include "clipper2next/geotypes/rect.hpp"

#include <ostream>

namespace clipper2next {

template <typename T>
using Rect = geotypes::Rect2<T>;

using Rect64 = geotypes::Rect2i64;
using RectD = geotypes::Rect2d;
using rect64 = Rect64;
using rectd = RectD;

template <typename T>
[[nodiscard]] constexpr auto invalid_rect() noexcept -> Rect<T> {
    return Rect<T>::invalid_rect();
}

template <typename T>
[[nodiscard]] constexpr auto is_valid(const Rect<T>& rect) noexcept -> bool {
    return rect.is_valid();
}

template <typename T>
[[nodiscard]] constexpr auto width(const Rect<T>& rect) noexcept -> T {
    return rect.width();
}

template <typename T>
[[nodiscard]] constexpr auto height(const Rect<T>& rect) noexcept -> T {
    return rect.height();
}

template <typename T, typename Value>
[[nodiscard]] inline auto with_width(const Rect<T>& rect, Value value) -> Rect<T> {
    return {rect.left,
            rect.top,
            geotypes::saturatedAddTruncatingDelta(rect.left, value),
            rect.bottom};
}

template <typename T, typename Value>
[[nodiscard]] inline auto with_height(const Rect<T>& rect, Value value) -> Rect<T> {
    return {rect.left,
            rect.top,
            rect.right,
            geotypes::saturatedAddTruncatingDelta(rect.top, value)};
}

template <typename T>
[[nodiscard]] constexpr auto midpoint(const Rect<T>& rect) noexcept -> Point<T> {
    return rect.midpoint();
}

template <typename T>
[[nodiscard]] inline auto as_path(const Rect<T>& rect) -> Path<T> {
    return {{rect.left, rect.top},
            {rect.right, rect.top},
            {rect.right, rect.bottom},
            {rect.left, rect.bottom}};
}

template <typename T>
[[nodiscard]] constexpr auto contains(const Rect<T>& rect,
                                      const Point<T>& point) noexcept -> bool {
    return rect.contains(point);
}

template <typename T>
[[nodiscard]] constexpr auto contains(const Rect<T>& outer,
                                      const Rect<T>& inner) noexcept -> bool {
    return outer.contains(inner);
}

template <typename T>
[[nodiscard]] constexpr auto is_empty(const Rect<T>& rect) noexcept -> bool {
    return rect.is_empty();
}

template <typename T>
[[nodiscard]] constexpr auto intersects(const Rect<T>& first,
                                        const Rect<T>& second) noexcept -> bool {
    return first.intersects(second);
}

template <typename T>
[[nodiscard]] constexpr auto union_bounds(const Rect<T>& first,
                                          const Rect<T>& second) noexcept
    -> Rect<T> {
    return first.union_bounds(second);
}

template <typename Target, typename Source>
[[nodiscard]] inline auto scale_rect_coordinate(Source coordinate,
                                                double scale) -> Target {
    return geotypes::coordinateCast<Target>(coordinate * scale);
}

template <typename T>
[[nodiscard]] inline auto scaled(const Rect<T>& rect, double scale) -> Rect<T> {
    return {
        scale_rect_coordinate<T>(rect.left, scale),
        scale_rect_coordinate<T>(rect.top, scale),
        scale_rect_coordinate<T>(rect.right, scale),
        scale_rect_coordinate<T>(rect.bottom, scale),
    };
}

template <typename T1, typename T2>
[[nodiscard]] inline auto scale_rect(const Rect<T2>& rect, double scale)
    -> Rect<T1> {
    return {
        scale_rect_coordinate<T1>(rect.left, scale),
        scale_rect_coordinate<T1>(rect.top, scale),
        scale_rect_coordinate<T1>(rect.right, scale),
        scale_rect_coordinate<T1>(rect.bottom, scale),
    };
}

template <typename T>
auto write_rect(std::ostream& stream, const Rect<T>& rect) -> std::ostream& {
    return stream << '(' << rect.left << ',' << rect.top << ','
                  << rect.right << ',' << rect.bottom << ") ";
}

}  // namespace clipper2next
