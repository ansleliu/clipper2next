#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "clipper2next/core/path.h"
#include "clipper2next/core/rect.h"

namespace clipper2next::benchmarks::product {

inline auto rectangle_path(int64_t left, int64_t top, int64_t right, int64_t bottom) -> Path64 {
    return Path64{{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

inline auto make_clip_subjects(std::size_t count) -> Paths64 {
    Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto column = static_cast<int64_t>(index % 8U);
        const auto row = static_cast<int64_t>(index / 8U);
        const int64_t left = 30 + column * 96;
        const int64_t top = 25 + row * 82;
        paths.push_back(rectangle_path(left, top, left + 72, top + 58));
    }
    return paths;
}

inline auto make_clip_windows(std::size_t count) -> Paths64 {
    Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto column = static_cast<int64_t>(index % 8U);
        const auto row = static_cast<int64_t>(index / 8U);
        const int64_t left = 62 + column * 96;
        const int64_t top = 47 + row * 82;
        paths.push_back(rectangle_path(left, top, left + 76, top + 64));
    }
    return paths;
}

inline auto make_offset_subjects(std::size_t count) -> Paths64 {
    Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto column = static_cast<int64_t>(index % 10U);
        const auto row = static_cast<int64_t>(index / 10U);
        const int64_t x = 80 + column * 140;
        const int64_t y = 80 + row * 125;
        paths.push_back(Path64{
            {x, y},
            {x + 70, y - 12},
            {x + 112, y + 38},
            {x + 74, y + 98},
            {x + 10, y + 86},
            {x - 24, y + 28},
        });
    }
    return paths;
}

inline auto make_rectclip_subjects(std::size_t count) -> Paths64 {
    Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto column = static_cast<int64_t>(index % 12U);
        const auto row = static_cast<int64_t>(index / 12U);
        const int64_t x = -180 + column * 88;
        const int64_t y = -120 + row * 74;
        paths.push_back(Path64{
            {x, y},
            {x + 146, y + 18},
            {x + 132, y + 104},
            {x + 36, y + 142},
            {x - 26, y + 64},
        });
    }
    return paths;
}

inline auto make_triangulation_subject(std::size_t point_count) -> Paths64 {
    constexpr double pi = 3.141592653589793238462643383279502884;
    Path64 polygon;
    polygon.reserve(point_count);
    for (std::size_t index = 0; index < point_count; ++index) {
        const double angle =
            (2.0 * pi * static_cast<double>(index)) / static_cast<double>(point_count);
        const auto x = static_cast<int64_t>(std::llround(1000.0 + 420.0 * std::cos(angle)));
        const auto y = static_cast<int64_t>(std::llround(1000.0 + 290.0 * std::sin(angle)));
        polygon.emplace_back(x, y);
    }
    return Paths64{polygon};
}

}  // namespace clipper2next::benchmarks::product
