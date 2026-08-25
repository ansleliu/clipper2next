// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/geometry.h"
#include "clipper2next/geometry/scaling_policy.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace clipper2next {

template <typename T1, typename T2>
[[nodiscard]] inline auto scaled_bounds_fit(const Path<T2>& path, double scale_x, double scale_y)
    -> bool {
    if constexpr (!std::is_integral_v<T1>) {
        return true;
    } else {
        if (path.empty()) { return true; }
        const auto path_bounds = bounds(path);
        return scaled_coordinate_fits(path_bounds.left, scale_x) &&
               scaled_coordinate_fits(path_bounds.right, scale_x) &&
               scaled_coordinate_fits(path_bounds.top, scale_y) &&
               scaled_coordinate_fits(path_bounds.bottom, scale_y);
    }
}

namespace path_scaling {

// Conversion loop without range validation; callers are responsible for
// running scaled_bounds_fit (once per path set, not per path) beforehand.
template <typename T1, typename T2>
[[nodiscard]] inline auto scale_path_unchecked(const Path<T2>& path,
                                               double scale_x,
                                               double scale_y)
    -> Path<T1> {
    Path<T1> scaled;
    if constexpr (!std::is_integral_v<T1>) {
        const auto count = path.size();
        scaled.resize(count);
        const auto* source = path.data();
        auto* target = scaled.data();
        for (std::size_t index = 0; index < count; ++index) {
            target[index].x = static_cast<T1>(source[index].x) * scale_x;
            target[index].y = static_cast<T1>(source[index].y) * scale_y;
        }
    } else {
        scaled.reserve(path.size());
        if constexpr (std::is_integral_v<T2>) {
            if (scale_x == 1.0 && scale_y == 1.0) {
                for (const auto& point : path) {
                    scaled.emplace_back(
                        static_cast<T1>(point.x), static_cast<T1>(point.y));
                }
                return scaled;
            }
        }
        for (const auto& point : path) {
            scaled.emplace_back(Point<T1>{
                geotypes::coordinateCast<T1>(
                    static_cast<double>(point.x) * scale_x),
                geotypes::coordinateCast<T1>(
                    static_cast<double>(point.y) * scale_y)});
        }
    }
    return scaled;
}

}  // namespace path_scaling

template <typename T1, typename T2>
[[nodiscard]] inline auto scale_path(
    const Path<T2>& path,
    double scale_x,
    double scale_y)
    -> clipper_result<Path<T1>> {
    if (scale_x == 0.0 || scale_y == 0.0) {
        return make_clipper_error<Path<T1>>(clipper_error_code::scale_out_of_range);
    }

    if (!scaled_bounds_fit<T1, T2>(path, scale_x, scale_y)) {
        return make_clipper_error<Path<T1>>(clipper_error_code::coordinate_range);
    }

    return path_scaling::scale_path_unchecked<T1, T2>(path, scale_x, scale_y);
}

template <typename T1, typename T2>
[[nodiscard]] inline auto scale_path(
    const Path<T2>& path,
    double scale)
    -> clipper_result<Path<T1>> {
    return scale_path<T1, T2>(path, scale, scale);
}

template <typename T1, typename T2>
[[nodiscard]] inline auto scaled_bounds_fit(const Paths<T2>& paths, double scale_x, double scale_y)
    -> bool {
    if constexpr (!std::is_integral_v<T1>) {
        return true;
    } else {
        if (paths.empty()) { return true; }
        const auto path_bounds = bounds(paths);
        return scaled_coordinate_fits(path_bounds.left, scale_x) &&
               scaled_coordinate_fits(path_bounds.right, scale_x) &&
               scaled_coordinate_fits(path_bounds.top, scale_y) &&
               scaled_coordinate_fits(path_bounds.bottom, scale_y);
    }
}

template <typename T1, typename T2>
[[nodiscard]] inline auto scale_paths(
    const Paths<T2>& paths,
    double scale_x,
    double scale_y)
    -> clipper_result<Paths<T1>> {
    if (scale_x == 0.0 || scale_y == 0.0) {
        return make_clipper_error<Paths<T1>>(clipper_error_code::scale_out_of_range);
    }

    if (!scaled_bounds_fit<T1, T2>(paths, scale_x, scale_y)) {
        return make_clipper_error<Paths<T1>>(clipper_error_code::coordinate_range);
    }

    Paths<T1> scaled;
    scaled.reserve(paths.size());
    for (const auto& path : paths) {
        // The combined-bounds check above already validated every path, so the
        // per-path conversion can skip a second O(n) bounds scan.
        scaled.push_back(
            path_scaling::scale_path_unchecked<T1, T2>(path, scale_x, scale_y));
    }
    return scaled;
}

template <typename T1, typename T2>
[[nodiscard]] inline auto scale_paths(
    const Paths<T2>& paths,
    double scale)
    -> clipper_result<Paths<T1>> {
    return scale_paths<T1, T2>(paths, scale, scale);
}

}  // namespace clipper2next
