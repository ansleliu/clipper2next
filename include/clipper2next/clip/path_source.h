// SPDX-License-Identifier: BSL-1.0
#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/clip/types.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <ranges>
#include <type_traits>
#include <utility>

namespace clipper2next::path_source_contract {

struct borrowed_path_measurement64 final {
    std::size_t source_point_count{};
    std::size_t normalized_point_count{};
};

template <typename Point>
concept integral_xy_point = requires(const Point& point) {
    point.x;
    point.y;
} && std::integral<std::remove_cvref_t<decltype(std::declval<const Point&>().x)>> &&
    std::integral<std::remove_cvref_t<decltype(std::declval<const Point&>().y)>> &&
    (!std::same_as<std::remove_cvref_t<decltype(std::declval<const Point&>().x)>, bool>) &&
    (!std::same_as<std::remove_cvref_t<decltype(std::declval<const Point&>().y)>, bool>);

template <typename Paths>
concept borrowable_paths64_source =
    std::ranges::random_access_range<const std::remove_reference_t<Paths>> &&
    std::ranges::sized_range<const std::remove_reference_t<Paths>> &&
    std::ranges::forward_range<std::ranges::range_reference_t<
        const std::remove_reference_t<Paths>>> &&
    std::ranges::sized_range<std::ranges::range_reference_t<
        const std::remove_reference_t<Paths>>> &&
    integral_xy_point<std::ranges::range_reference_t<std::ranges::range_reference_t<
        const std::remove_reference_t<Paths>>>>;

template <std::integral Coordinate>
[[nodiscard]] constexpr auto coordinate_to_int64(Coordinate value,
                                                  std::int64_t& result) noexcept -> bool {
    using value_type = std::remove_cv_t<Coordinate>;
    if constexpr (std::is_signed_v<value_type>) {
        if constexpr (sizeof(value_type) > sizeof(std::int64_t)) {
            if (value < static_cast<value_type>((std::numeric_limits<std::int64_t>::min)()) ||
                value > static_cast<value_type>((std::numeric_limits<std::int64_t>::max)())) {
                return false;
            }
        }
    } else if constexpr (sizeof(value_type) >= sizeof(std::int64_t)) {
        if (value > static_cast<value_type>((std::numeric_limits<std::int64_t>::max)())) {
            return false;
        }
    }
    result = static_cast<std::int64_t>(value);
    return true;
}

template <integral_xy_point Point>
[[nodiscard]] constexpr auto point_to_point64(const Point& source, Point64& result) noexcept
    -> bool {
    return coordinate_to_int64(source.x, result.x) && coordinate_to_int64(source.y, result.y);
}

template <typename Source>
[[nodiscard]] auto borrowed_path_count(const void* source,
                                       std::size_t& result) noexcept -> clipper_error_code {
    try {
        result = static_cast<std::size_t>(std::ranges::size(*static_cast<const Source*>(source)));
        return clipper_error_code::ok;
    } catch (const std::bad_alloc&) {
        return clipper_error_code::allocation_failure;
    } catch (...) {
        return clipper_error_code::input_access_failure;
    }
}

template <typename Source>
[[nodiscard]] auto measure_borrowed_path(const void* source,
                                         std::size_t path_index,
                                         borrowed_path_measurement64& result) noexcept
    -> clipper_error_code {
    try {
        const auto& paths = *static_cast<const Source*>(source);
        if (path_index >= static_cast<std::size_t>(std::ranges::size(paths))) {
            return clipper_error_code::input_changed;
        }
        const auto& path = *(std::ranges::begin(paths) + path_index);
        result = {};
        result.source_point_count = static_cast<std::size_t>(std::ranges::size(path));

        Point64 first{};
        Point64 last{};
        bool has_point = false;
        for (const auto& source_point : path) {
            Point64 point{};
            if (!point_to_point64(source_point, point)) {
                return clipper_error_code::coordinate_range;
            }
            if (has_point && point == last) { continue; }
            if (!has_point) {
                first = point;
                has_point = true;
            }
            last = point;
            ++result.normalized_point_count;
        }
        if (result.normalized_point_count > 1U && last == first) {
            --result.normalized_point_count;
        }
        return clipper_error_code::ok;
    } catch (const std::bad_alloc&) {
        return clipper_error_code::allocation_failure;
    } catch (...) {
        return clipper_error_code::input_access_failure;
    }
}

template <typename Source>
[[nodiscard]] auto copy_borrowed_path(const void* source,
                                      std::size_t path_index,
                                      Point64* destination,
                                      std::size_t destination_stride,
                                      std::size_t destination_capacity,
                                      std::size_t expected_normalized_count,
                                      std::size_t& normalized_count,
                                      std::size_t& point_write_count) noexcept
    -> clipper_error_code {
    try {
        const auto& paths = *static_cast<const Source*>(source);
        if (path_index >= static_cast<std::size_t>(std::ranges::size(paths))) {
            return clipper_error_code::input_changed;
        }
        const auto& path = *(std::ranges::begin(paths) + path_index);
        if (static_cast<std::size_t>(std::ranges::size(path)) != destination_capacity) {
            return clipper_error_code::input_changed;
        }

        auto* destination_bytes = reinterpret_cast<std::byte*>(destination);
        Point64 first{};
        Point64 last{};
        bool has_point = false;
        normalized_count = 0U;
        point_write_count = 0U;
        for (const auto& source_point : path) {
            Point64 point{};
            if (!point_to_point64(source_point, point)) {
                return clipper_error_code::coordinate_range;
            }
            if (has_point && point == last) { continue; }
            if (point_write_count >= destination_capacity) {
                return clipper_error_code::input_changed;
            }
            if (!has_point) {
                first = point;
                has_point = true;
            }
            *reinterpret_cast<Point64*>(destination_bytes + point_write_count * destination_stride) =
                point;
            last = point;
            ++point_write_count;
        }
        normalized_count = point_write_count;
        if (normalized_count > 1U && last == first) { --normalized_count; }
        if (normalized_count != expected_normalized_count) {
            return clipper_error_code::input_changed;
        }
        return clipper_error_code::ok;
    } catch (const std::bad_alloc&) {
        return clipper_error_code::allocation_failure;
    } catch (...) {
        return clipper_error_code::input_access_failure;
    }
}

}  // namespace clipper2next::path_source_contract
