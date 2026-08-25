#pragma once

#include "clipper2next/core/rect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <type_traits>

namespace clipper2next {

template <typename T>
struct bounds_accumulator {
    T xmin = (std::numeric_limits<T>::max)();
    T ymin = (std::numeric_limits<T>::max)();
    T xmax = (std::numeric_limits<T>::lowest)();
    T ymax = (std::numeric_limits<T>::lowest)();

    template <typename PointT>
    void include(const PointT& point) {
        const auto x_value = static_cast<T>(point.x);
        const auto y_value = static_cast<T>(point.y);
        xmin = (std::min)(xmin, x_value);
        xmax = (std::max)(xmax, x_value);
        ymin = (std::min)(ymin, y_value);
        ymax = (std::max)(ymax, y_value);
    }

    [[nodiscard]] auto rect() const -> Rect<T> { return Rect<T>{xmin, ymin, xmax, ymax}; }
};

template <typename Result, typename Source>
[[nodiscard]] inline auto bounds_for_path(const Path<Source>& path) -> Rect<Result> {
    bounds_accumulator<Result> bounds;
    for (const auto& point : path) { bounds.include(point); }
    return bounds.rect();
}

template <typename Result, typename Source>
[[nodiscard]] inline auto bounds_for_paths(const Paths<Source>& paths) -> Rect<Result> {
    bounds_accumulator<Result> bounds;
    for (const auto& path : paths) {
        for (const auto& point : path) { bounds.include(point); }
    }
    return bounds.rect();
}

template <typename T>
[[nodiscard]] inline auto bounds(const Path<T>& path) -> Rect<T> {
    return bounds_for_path<T, T>(path);
}

template <typename T>
[[nodiscard]] inline auto bounds(const Paths<T>& paths) -> Rect<T> {
    return bounds_for_paths<T, T>(paths);
}

template <typename T, typename T2>
[[nodiscard]] inline auto bounds(const Path<T2>& path) -> Rect<T> {
    return bounds_for_path<T, T2>(path);
}

template <typename T, typename T2>
[[nodiscard]] inline auto bounds(const Paths<T2>& paths) -> Rect<T> {
    return bounds_for_paths<T, T2>(paths);
}

// Accumulates in double with Neumaier compensation rather than long double:
// long double is 64-bit on MSVC but 80-bit on Linux x86-64, so it makes area()
// (and therefore is_positive orientation on borderline inputs) platform-dependent.
namespace area_accumulation {

template <typename PathLike>
[[nodiscard]] inline auto area_for_path(const PathLike& path) -> double {
    if (path.size() < 3) { return 0.0; }

    double twice_area = 0.0;
    double compensation = 0.0;
    std::size_t previous = path.size() - 1U;
    for (std::size_t current = 0; current < path.size(); ++current) {
        const auto y_sum =
            static_cast<double>(path[previous].y) + static_cast<double>(path[current].y);
        const auto x_delta =
            static_cast<double>(path[previous].x) - static_cast<double>(path[current].x);
        const auto term = y_sum * x_delta;
        const auto next_sum = twice_area + term;
        if (std::abs(twice_area) >= std::abs(term)) {
            compensation += (twice_area - next_sum) + term;
        } else {
            compensation += (term - next_sum) + twice_area;
        }
        twice_area = next_sum;
        previous = current;
    }
    return (twice_area + compensation) * 0.5;
}

}  // namespace area_accumulation

template <typename T>
[[nodiscard]] inline auto area(const Path<T>& path) -> double {
    return area_accumulation::area_for_path(path);
}

[[nodiscard]] inline auto area(std::span<const Point64> path) -> double {
    return area_accumulation::area_for_path(path);
}

template <typename T>
[[nodiscard]] inline auto area(const Paths<T>& paths) -> double {
    double total = 0.0;
    for (const auto& path : paths) { total += area(path); }
    return total;
}

template <typename T>
[[nodiscard]] inline auto is_positive(const Path<T>& polygon) -> bool {
    return area(polygon) >= 0.0;
}

[[nodiscard]] inline auto is_positive(std::span<const Point64> polygon) -> bool {
    return area(polygon) >= 0.0;
}

}  // namespace clipper2next
