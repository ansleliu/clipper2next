// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/geometry/math.h"

#include <algorithm>
#include <type_traits>

namespace clipper2next {

template <typename T1, typename T2>
[[nodiscard]] inline auto transform_path(const Path<T2>& path) -> Path<T1> {
    const auto count = path.size();
    Path<T1> transformed;
    transformed.resize(count);
    const auto* source = path.data();
    auto* target = transformed.data();
    if constexpr (std::is_same_v<T1, T2>) {
        for (std::size_t index = 0; index < count; ++index) { target[index] = source[index]; }
    } else {
        for (std::size_t index = 0; index < count; ++index) {
            target[index].x = geotypes::truncatingCoordinateCast<T1>(source[index].x);
            target[index].y = geotypes::truncatingCoordinateCast<T1>(source[index].y);
        }
    }
    return transformed;
}

template <typename T1, typename T2>
[[nodiscard]] inline auto transform_paths(const Paths<T2>& paths) -> Paths<T1> {
    Paths<T1> transformed;
    transformed.reserve(paths.size());
    for (const auto& path : paths) { transformed.push_back(transform_path<T1, T2>(path)); }
    return transformed;
}

template <typename T>
[[nodiscard]] inline auto strip_near_equal(const Path<T>& path,
                                           double max_dist_sqrd,
                                           bool is_closed_path) -> Path<T> {
    Path<T> result;
    result.reserve(path.size());
    for (const auto& point : path) {
        if (result.empty() || !near_equal(point, result.back(), max_dist_sqrd)) {
            result.push_back(point);
        }
    }
    if (is_closed_path && !result.empty()) {
        const auto first = result.front();
        while (result.size() > 1 && near_equal(result.back(), first, max_dist_sqrd)) {
            result.pop_back();
        }
    }
    return result;
}

template <typename T>
[[nodiscard]] inline auto strip_near_equal(const Paths<T>& paths,
                                           double max_dist_sqrd,
                                           bool is_closed_path) -> Paths<T> {
    Paths<T> result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        result.push_back(strip_near_equal(path, max_dist_sqrd, is_closed_path));
    }
    return result;
}

}  // namespace clipper2next
