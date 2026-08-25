#pragma once

#include "clipper2next/geotypes/point.hpp"

#include <algorithm>
#include <limits>

namespace geotypes {

// Closed axis-aligned coordinate bounds: [left, right] x [top, bottom].
// New domain APIs should represent optional bounds with std::optional; the
// explicit invalid_rect sentinel remains available for bounds accumulation.
template <class T>
struct Rect2 final {
    T left{};
    T top{};
    T right{};
    T bottom{};

    [[nodiscard]] static constexpr auto invalid_rect() noexcept -> Rect2 {
        return {
            (std::numeric_limits<T>::max)(),
            (std::numeric_limits<T>::max)(),
            (std::numeric_limits<T>::lowest)(),
            (std::numeric_limits<T>::lowest)(),
        };
    }

    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool {
        return left != (std::numeric_limits<T>::max)();
    }

    [[nodiscard]] constexpr auto width() const noexcept -> T {
        return saturatedSubtract(right, left);
    }

    [[nodiscard]] constexpr auto height() const noexcept -> T {
        return saturatedSubtract(bottom, top);
    }

    [[nodiscard]] constexpr auto midpoint() const noexcept -> Point2<T> {
        return {midpointCoordinate(left, right), midpointCoordinate(top, bottom)};
    }

    [[nodiscard]] constexpr auto contains(const Point2<T>& point) const noexcept
        -> bool {
        return point.x > left && point.x < right &&
               point.y > top && point.y < bottom;
    }

    [[nodiscard]] constexpr auto contains(const Rect2& rect) const noexcept
        -> bool {
        return rect.left >= left && rect.right <= right &&
               rect.top >= top && rect.bottom <= bottom;
    }

    [[nodiscard]] constexpr auto is_empty() const noexcept -> bool {
        return bottom <= top || right <= left;
    }

    [[nodiscard]] constexpr auto intersects(const Rect2& rect) const noexcept
        -> bool {
        const auto xOverlap =
            (std::max)(left, rect.left) <= (std::min)(right, rect.right);
        const auto yOverlap =
            (std::max)(top, rect.top) <= (std::min)(bottom, rect.bottom);
        return xOverlap && yOverlap;
    }

    [[nodiscard]] constexpr auto union_bounds(const Rect2& rect) const noexcept
        -> Rect2 {
        return {
            (std::min)(left, rect.left),
            (std::min)(top, rect.top),
            (std::max)(right, rect.right),
            (std::max)(bottom, rect.bottom),
        };
    }

    constexpr auto operator+=(const Rect2& rect) noexcept -> Rect2& {
        *this = union_bounds(rect);
        return *this;
    }

    [[nodiscard]] constexpr auto operator+(const Rect2& rect) const noexcept
        -> Rect2 {
        return union_bounds(rect);
    }

    friend constexpr auto operator==(const Rect2&, const Rect2&) -> bool = default;
};

using Rect2i32 = Rect2<std::int32_t>;
using Rect2i64 = Rect2<std::int64_t>;
using Rect2f = Rect2<float>;
using Rect2d = Rect2<double>;

}  // namespace geotypes
