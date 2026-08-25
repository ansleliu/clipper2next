// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/core/path.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace clipper2next {

namespace coordinate_differences {

template <typename T>
[[nodiscard]] inline auto coordinate_difference_as_double(T left, T right) -> double {
    if constexpr (std::is_integral_v<T>) {
        using unsigned_type = std::make_unsigned_t<T>;
        if (left >= right) {
            return static_cast<double>(static_cast<unsigned_type>(left) -
                                       static_cast<unsigned_type>(right));
        }
        return -static_cast<double>(static_cast<unsigned_type>(right) -
                                    static_cast<unsigned_type>(left));
    } else {
        return static_cast<double>(left) - static_cast<double>(right);
    }
}

}  // namespace coordinate_differences

[[nodiscard]] inline auto round_to_even_int64(double value) noexcept -> int64_t {
    if (std::isnan(value)) { return 0; }

    constexpr auto lower_bound = -0x1p63;
    constexpr auto upper_bound = 0x1p63;
    if (value <= lower_bound) { return (std::numeric_limits<int64_t>::lowest)(); }
    if (value >= upper_bound) { return (std::numeric_limits<int64_t>::max)(); }

    return static_cast<int64_t>(geotypes::roundToEven(value));
}

template <typename T>
[[nodiscard]] inline auto square(T value) -> double {
    const auto as_double = static_cast<double>(value);
    return as_double * as_double;
}

template <typename T>
[[nodiscard]] inline auto distance_squared(Point<T> first, Point<T> second) -> double {
    return square(coordinate_differences::coordinate_difference_as_double(first.x, second.x)) +
           square(coordinate_differences::coordinate_difference_as_double(first.y, second.y));
}

template <typename T>
[[nodiscard]] inline auto near_equal(const Point<T>& first,
                                     const Point<T>& second,
                                     double max_dist_sqrd) -> bool {
    return distance_squared(first, second) < max_dist_sqrd;
}

[[nodiscard]] inline auto tri_sign(int64_t value) -> int {
    return (value > 0) - (value < 0);
}

struct UInt128Struct {
    uint64_t lo = 0;
    uint64_t hi = 0;

    [[nodiscard]] auto operator==(const UInt128Struct& other) const -> bool {
        return lo == other.lo && hi == other.hi;
    }
};

[[nodiscard]] inline auto multiply_uint64(uint64_t first, uint64_t second) -> UInt128Struct {
    constexpr uint64_t mask32 = 0xFFFF'FFFFULL;
    const uint64_t first_low = first & mask32;
    const uint64_t first_high = first >> 32U;
    const uint64_t second_low = second & mask32;
    const uint64_t second_high = second >> 32U;

    const uint64_t low_product = first_low * second_low;
    const uint64_t middle_a = first_high * second_low + (low_product >> 32U);
    const uint64_t middle_b = first_low * second_high + (middle_a & mask32);
    const uint64_t high_product = first_high * second_high + (middle_a >> 32U) + (middle_b >> 32U);

    return UInt128Struct{
        ((middle_b & mask32) << 32U) | (low_product & mask32),
        high_product,
    };
}

template <typename T>
[[nodiscard]] inline auto dot_product(const Point<T>& first,
                                      const Point<T>& middle,
                                      const Point<T>& last) -> double {
    const auto ax = coordinate_differences::coordinate_difference_as_double(middle.x, first.x);
    const auto ay = coordinate_differences::coordinate_difference_as_double(middle.y, first.y);
    const auto bx = coordinate_differences::coordinate_difference_as_double(last.x, middle.x);
    const auto by = coordinate_differences::coordinate_difference_as_double(last.y, middle.y);
    return ax * bx + ay * by;
}

template <typename T>
[[nodiscard]] inline auto dot_product(const Point<T>& first, const Point<T>& second) -> double {
    // Convert before multiplying: the coordinate product can exceed int64 range.
    return static_cast<double>(first.x) * static_cast<double>(second.x) +
           static_cast<double>(first.y) * static_cast<double>(second.y);
}

template <typename T>
struct line_bounds {
    T min_x{};
    T min_y{};
    T max_x{};
    T max_y{};
};

template <typename T>
[[nodiscard]] inline auto make_line_bounds(const Point<T>& first, const Point<T>& second)
    -> line_bounds<T> {
    return line_bounds<T>{
        (std::min)(first.x, second.x),
        (std::min)(first.y, second.y),
        (std::max)(first.x, second.x),
        (std::max)(first.y, second.y),
    };
}

template <typename T>
[[nodiscard]] inline auto translate_point(const Point<T>& point, double dx, double dy) -> Point<T> {
    return Point<T>{point.x + dx, point.y + dy};
}

template <typename T>
[[nodiscard]] inline auto reflect_point(const Point<T>& point, const Point<T>& pivot) -> Point<T> {
    return Point<T>{
        geotypes::saturatedReflect(point.x, pivot.x),
        geotypes::saturatedReflect(point.y, pivot.y)};
}

template <typename T>
[[nodiscard]] inline auto sign(const T& value) -> int {
    return (value > T{}) - (value < T{});
}

template <typename T>
[[nodiscard]] inline auto closest_point_on_segment(const Point<T>& point,
                                                   const Point<T>& segment_start,
                                                   const Point<T>& segment_end) -> Point<T> {
    if (segment_start == segment_end) { return segment_start; }

    const double dx =
        coordinate_differences::coordinate_difference_as_double(segment_end.x, segment_start.x);
    const double dy =
        coordinate_differences::coordinate_difference_as_double(segment_end.y, segment_start.y);
    const double numerator =
        coordinate_differences::coordinate_difference_as_double(point.x, segment_start.x) * dx +
        coordinate_differences::coordinate_difference_as_double(point.y, segment_start.y) * dy;
    const double ratio = std::clamp(numerator / (square(dx) + square(dy)), 0.0, 1.0);

    if constexpr (std::is_integral_v<T>) {
        return Point<T>{
            geotypes::saturatedAddSignedDelta(segment_start.x,
                                                     round_to_even_int64(ratio * dx)),
            geotypes::saturatedAddSignedDelta(segment_start.y,
                                                     round_to_even_int64(ratio * dy)),
        };
    } else {
        return Point<T>{
            segment_start.x + static_cast<T>(ratio * dx),
            segment_start.y + static_cast<T>(ratio * dy),
        };
    }
}

}  // namespace clipper2next
