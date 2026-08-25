#include "clipper2next/geometry/algorithms.h"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace clipper2next {
namespace {

[[nodiscard]] inline auto round_away_from_zero_to_i64(double value) -> int64_t {
    return value >= 0.0 ? static_cast<int64_t>(value + 0.5) : static_cast<int64_t>(value - 0.5);
}

[[nodiscard]] auto ellipse_extent_is_representable(int64_t center, double radius) noexcept
    -> bool {
    // INT64_MAX rounds to 2^63 as a double, so use the first positive value
    // outside int64 as an exclusive limit rather than converting INT64_MAX.
    constexpr double first_positive_outside_int64 = 9223372036854775808.0;
    constexpr double lowest_int64 = -first_positive_outside_int64;
    const auto center_as_double = static_cast<double>(center);
    const auto lower = center_as_double - radius;
    const auto upper = center_as_double + radius;
    return std::isfinite(lower) && std::isfinite(upper) && lower >= lowest_int64 &&
           upper < first_positive_outside_int64;
}

[[nodiscard]] auto make_point64_from_double(double x, double y) -> Point64 {
    Point64 point;
    point.x = round_away_from_zero_to_i64(x);
    point.y = round_away_from_zero_to_i64(y);
    return point;
}

[[nodiscard]] auto ellipse_rotation_step(std::size_t steps) -> std::pair<double, double> {
    if (steps == 32U) { return {0.19509032201612825, 0.98078528040323043}; }
    const auto angle = 2.0 * std::numbers::pi_v<double> / static_cast<double>(steps);
    return {std::sin(angle), std::cos(angle)};
}

[[nodiscard]] auto ellipse_unit_steps_32() -> const std::array<std::pair<double, double>, 32U>& {
    static const auto values = [] {
        auto result = std::array<std::pair<double, double>, 32U>{};
        result[0] = {1.0, 0.0};
        const auto [sine, cosine] = ellipse_rotation_step(32U);
        auto dx = cosine;
        auto dy = sine;
        for (std::size_t index = 1; index < result.size(); ++index) {
            result[index] = {dx, dy};
            const auto x = dx * cosine - dy * sine;
            dy = dy * cosine + dx * sine;
            dx = x;
        }
        return result;
    }();
    return values;
}

}  // namespace

auto make_ellipse(const Point64& center, double radius_x, double radius_y, std::size_t steps)
    -> Path64 {
    if (!std::isfinite(radius_x) || !std::isfinite(radius_y) || radius_x <= 0) { return {}; }
    if (radius_y <= 0) { radius_y = radius_x; }
    if (!ellipse_extent_is_representable(center.x, radius_x) ||
        !ellipse_extent_is_representable(center.y, radius_y)) {
        return {};
    }
    if (steps <= 2) {
        const auto automatic_steps =
            std::numbers::pi_v<double> * std::sqrt(radius_x * 0.5 + radius_y * 0.5);
        if (!std::isfinite(automatic_steps) ||
            automatic_steps >= static_cast<double>((std::numeric_limits<std::size_t>::max)())) {
            return {};
        }
        steps = static_cast<std::size_t>(automatic_steps);
    }

    const auto center_x = static_cast<double>(center.x);
    const auto center_y = static_cast<double>(center.y);
    Path64 result;
    result.reserve(steps);
    if (steps == 32U) {
        for (const auto& [dx, dy] : ellipse_unit_steps_32()) {
            result.push_back(
                make_point64_from_double(center_x + radius_x * dx, center_y + radius_y * dy));
        }
        return result;
    }

    const auto [sine, cosine] = ellipse_rotation_step(steps);
    auto dx = cosine;
    auto dy = sine;
    result.push_back(make_point64_from_double(center_x + radius_x, center_y));
    for (std::size_t index = 1; index < steps; ++index) {
        result.push_back(
            make_point64_from_double(center_x + radius_x * dx, center_y + radius_y * dy));
        const auto x = dx * cosine - dy * sine;
        dy = dy * cosine + dx * sine;
        dx = x;
    }
    return result;
}

auto make_ellipse(const Rect64& rect, std::size_t steps) -> Path64 {
    return make_ellipse(rect.midpoint(),
                        static_cast<double>(rect.width()) * 0.5,
                        static_cast<double>(rect.height()) * 0.5,
                        steps);
}

auto path_contains_path(const Path64& inner, const Path64& outer) -> bool {
    auto result = PointInPolygonResult::IsOn;
    for (const auto& point : inner) {
        switch (point_in_polygon(point, outer)) {
        case PointInPolygonResult::IsOutside: {
            if (result == PointInPolygonResult::IsOutside) { return false; }
            result = PointInPolygonResult::IsOutside;
            break;
        }
        case PointInPolygonResult::IsInside: {
            if (result == PointInPolygonResult::IsInside) { return true; }
            result = PointInPolygonResult::IsInside;
            break;
        }
        default: {
            break;
        }
        }
    }
    if (result != PointInPolygonResult::IsInside) { return false; }
    return point_in_polygon(bounds(inner).midpoint(), outer) == PointInPolygonResult::IsInside;
}

}  // namespace clipper2next
