// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/geometry/math.h"

namespace clipper2next {

enum class precision_mode {
    fast,
    precise,
};

struct predicate_policy {
    precision_mode mode = precision_mode::fast;
};

namespace line_intersection {

template <typename T>
[[nodiscard]] inline auto line_intersection_point_precise(const Point<T>& first_start,
                                                          const Point<T>& first_end,
                                                          const Point<T>& second_start,
                                                          const Point<T>& second_end,
                                                          Point<T>& intersection) -> bool {
    const double first_dy =
        coordinate_differences::coordinate_difference_as_double(first_end.y, first_start.y);
    const double first_dx =
        coordinate_differences::coordinate_difference_as_double(first_start.x, first_end.x);
    const double second_dy =
        coordinate_differences::coordinate_difference_as_double(second_end.y, second_start.y);
    const double second_dx =
        coordinate_differences::coordinate_difference_as_double(second_start.x, second_end.x);
    const double determinant = second_dy * first_dx - first_dy * second_dx;
    if (determinant == 0.0) { return false; }

    const auto first_bounds = make_line_bounds(first_start, first_end);
    const auto second_bounds = make_line_bounds(second_start, second_end);

    if constexpr (std::is_integral_v<T>) {
        // Use an overflow-free floor midpoint. Keeping the origin in the
        // coordinate type makes the public template valid for every signed
        // integral Point type, not only Point64.
        const auto floor_midpoint = [](T first, T second) constexpr noexcept -> T {
            const T quotient_sum = first / T{2} + second / T{2};
            const int remainder_sum = static_cast<int>(first % T{2}) +
                                      static_cast<int>(second % T{2});
            if constexpr (std::is_signed_v<T>) {
                if (remainder_sum < 0) { return static_cast<T>(quotient_sum - T{1}); }
                if (remainder_sum == 2) { return static_cast<T>(quotient_sum + T{1}); }
                return quotient_sum;
            } else {
                return static_cast<T>(quotient_sum + static_cast<T>(remainder_sum / 2));
            }
        };
        const T origin_x = floor_midpoint(
            (std::min)(first_bounds.max_x, second_bounds.max_x),
            (std::max)(first_bounds.min_x, second_bounds.min_x));
        const T origin_y = floor_midpoint(
            (std::min)(first_bounds.max_y, second_bounds.max_y),
            (std::max)(first_bounds.min_y, second_bounds.min_y));
        const double first_c =
            first_dy * coordinate_differences::coordinate_difference_as_double(first_start.x, origin_x) +
            first_dx * coordinate_differences::coordinate_difference_as_double(first_start.y, origin_y);
        const double second_c =
            second_dy * coordinate_differences::coordinate_difference_as_double(second_start.x, origin_x) +
            second_dx * coordinate_differences::coordinate_difference_as_double(second_start.y, origin_y);
        intersection.x = geotypes::saturatedAddSignedDelta(
            origin_x,
            round_to_even_int64((first_dx * second_c - second_dx * first_c) / determinant));
        intersection.y = geotypes::saturatedAddSignedDelta(
            origin_y,
            round_to_even_int64((second_dy * first_c - first_dy * second_c) / determinant));
    } else {
        const double origin_x = ((std::min)(first_bounds.max_x, second_bounds.max_x) +
                                 (std::max)(first_bounds.min_x, second_bounds.min_x)) /
                                2.0;
        const double origin_y = ((std::min)(first_bounds.max_y, second_bounds.max_y) +
                                 (std::max)(first_bounds.min_y, second_bounds.min_y)) /
                                2.0;
        const double first_c = first_dy * static_cast<double>(first_start.x - origin_x) +
                               first_dx * static_cast<double>(first_start.y - origin_y);
        const double second_c = second_dy * static_cast<double>(second_start.x - origin_x) +
                                second_dx * static_cast<double>(second_start.y - origin_y);
        intersection.x =
            origin_x + static_cast<T>((first_dx * second_c - second_dx * first_c) / determinant);
        intersection.y =
            origin_y + static_cast<T>((second_dy * first_c - first_dy * second_c) / determinant);
    }
    return true;
}

template <typename T>
[[nodiscard]] inline auto line_intersection_point_fast(const Point<T>& first_start,
                                                       const Point<T>& first_end,
                                                       const Point<T>& second_start,
                                                       const Point<T>& second_end,
                                                       Point<T>& intersection) -> bool {
    const double first_dx =
        coordinate_differences::coordinate_difference_as_double(first_end.x, first_start.x);
    const double first_dy =
        coordinate_differences::coordinate_difference_as_double(first_end.y, first_start.y);
    const double second_dx =
        coordinate_differences::coordinate_difference_as_double(second_end.x, second_start.x);
    const double second_dy =
        coordinate_differences::coordinate_difference_as_double(second_end.y, second_start.y);
    const double determinant = first_dy * second_dx - second_dy * first_dx;
    if (determinant == 0.0) { return false; }

    const double ratio =
        (coordinate_differences::coordinate_difference_as_double(first_start.x, second_start.x) *
             second_dy -
         coordinate_differences::coordinate_difference_as_double(first_start.y, second_start.y) *
             second_dx) /
                         determinant;
    if (ratio <= 0.0) {
        intersection = first_start;
    } else if (ratio >= 1.0) {
        intersection = first_end;
    } else {
        intersection.x =
            geotypes::truncatingCoordinateCast<T>(first_start.x + ratio * first_dx);
        intersection.y =
            geotypes::truncatingCoordinateCast<T>(first_start.y + ratio * first_dy);
    }
    return true;
}

}  // namespace line_intersection

template <typename T>
[[nodiscard]] inline auto line_intersection_point(const Point<T>& first_start,
                                                  const Point<T>& first_end,
                                                  const Point<T>& second_start,
                                                  const Point<T>& second_end,
                                                  Point<T>& intersection,
                                                  predicate_policy policy) -> bool {
    if (policy.mode == precision_mode::precise) {
        return line_intersection::line_intersection_point_precise(
            first_start, first_end, second_start, second_end, intersection);
    }
    return line_intersection::line_intersection_point_fast(
        first_start, first_end, second_start, second_end, intersection);
}

template <typename T>
[[nodiscard]] inline auto line_intersection_point(const Point<T>& first_start,
                                                  const Point<T>& first_end,
                                                  const Point<T>& second_start,
                                                  const Point<T>& second_end,
                                                  Point<T>& intersection) -> bool {
    return line_intersection_point(
        first_start, first_end, second_start, second_end, intersection, predicate_policy{});
}

}  // namespace clipper2next
