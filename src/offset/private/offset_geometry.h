#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

#include "geometry/private/numeric_policy.h"
#include "clipper2next/offset/types.h"

namespace clipper2next::internal {

inline constexpr auto offset_floating_point_tolerance = 1e-12;
inline constexpr auto default_arc_tolerance_factor = 0.002;

struct offset_arc_parameters final {
    double steps_per_rad{0.0};
    double step_sin{0.0};
    double step_cos{0.0};
};

[[nodiscard]] inline auto arc_step_count(const offset_arc_parameters& arc,
                                         double angle) noexcept -> std::size_t {
    if (!std::isfinite(arc.steps_per_rad) || !std::isfinite(angle) ||
        arc.steps_per_rad <= 0.0) {
        return 1U;
    }
    const auto rounded = std::ceil(arc.steps_per_rad * std::abs(angle));
    const auto maximum = static_cast<double>((std::numeric_limits<std::size_t>::max)());
    if (!std::isfinite(rounded) || rounded >= maximum) {
        return (std::numeric_limits<std::size_t>::max)();
    }
    return std::max<std::size_t>(1U, static_cast<std::size_t>(rounded));
}

[[nodiscard]] inline auto hypot(double x, double y) -> double {
    return std::sqrt(x * x + y * y);
}

[[nodiscard]] inline auto almost_zero(double value, double epsilon = 0.001) -> bool {
    return std::fabs(value) < epsilon;
}

[[nodiscard]] inline auto unit_normal(const Point64& first, const Point64& second) -> PointD {
    if (first == second) { return {0.0, 0.0}; }

    auto delta_x = static_cast<double>(second.x - first.x);
    auto delta_y = static_cast<double>(second.y - first.y);
    const auto inverse_length = 1.0 / internal::hypot(delta_x, delta_y);
    delta_x *= inverse_length;
    delta_y *= inverse_length;
    return {delta_y, -delta_x};
}

[[nodiscard]] inline auto normalize_vector(const PointD& vector) -> PointD {
    const auto length = internal::hypot(vector.x, vector.y);
    if (internal::almost_zero(length)) { return {0.0, 0.0}; }

    const auto inverse_length = 1.0 / length;
    return {vector.x * inverse_length, vector.y * inverse_length};
}

[[nodiscard]] inline auto average_unit_vector(const PointD& first, const PointD& second) -> PointD {
    return internal::normalize_vector({first.x + second.x, first.y + second.y});
}

[[nodiscard]] inline auto is_closed_path(EndType end_type) -> bool {
    return end_type == EndType::Polygon || end_type == EndType::Joined;
}

[[nodiscard]] inline auto perpendicular_point(const Point64& point,
                                              const PointD& normal,
                                              double delta,
                                              const geotypes::CoordinateRounding rounding =
                                                  geotypes::CoordinateRounding::NearestEven)
    -> Point64 {
    return {
        geotypes::coordinateCast<std::int64_t>(point.x + normal.x * delta, rounding),
        geotypes::coordinateCast<std::int64_t>(point.y + normal.y * delta, rounding)};
}

[[nodiscard]] inline auto perpendicular_point_d(const Point64& point,
                                                const PointD& normal,
                                                double delta) -> PointD {
    return {point.x + normal.x * delta, point.y + normal.y * delta};
}

inline auto negate_path(PathD& path) -> void {
    for (auto& point : path) {
        point.x = -point.x;
        point.y = -point.y;
    }
}

inline auto assign_normals(PathD& normals, std::span<const Point64> path) -> void {
    normals.clear();
    normals.reserve(path.size());

    if (path.empty()) { return; }

    for (auto path_iter = path.cbegin(); path_iter != std::prev(path.cend()); ++path_iter) {
        normals.emplace_back(internal::unit_normal(*path_iter, *(path_iter + 1)));
    }

    normals.emplace_back(internal::unit_normal(path.back(), path.front()));
}

[[nodiscard]] inline auto build_normals(std::span<const Point64> path) -> PathD {
    PathD normals;
    internal::assign_normals(normals, path);
    return normals;
}

[[nodiscard]] inline auto make_arc_parameters(
    double delta,
    double arc_tolerance,
    const std::size_t segments_per_quadrant = 0U)
    -> offset_arc_parameters {
    if (!std::isfinite(delta) || !std::isfinite(arc_tolerance)) { return {}; }
    const auto abs_delta = std::fabs(delta);
    if (abs_delta <= internal::offset_floating_point_tolerance) { return {}; }

    const auto steps_per_circle = [&] {
        if (segments_per_quadrant != 0U) {
            return static_cast<double>(segments_per_quadrant) * 4.0;
        }
        const auto effective_arc_tolerance =
            arc_tolerance > internal::offset_floating_point_tolerance
                ? std::min(abs_delta, arc_tolerance)
                : abs_delta * internal::default_arc_tolerance_factor;
        return std::min(
            pi / std::acos(1 - effective_arc_tolerance / abs_delta),
            abs_delta * pi);
    }();

    offset_arc_parameters parameters;
    parameters.step_sin = std::sin(2 * pi / steps_per_circle);
    parameters.step_cos = std::cos(2 * pi / steps_per_circle);
    if (delta < 0.0) { parameters.step_sin = -parameters.step_sin; }
    parameters.steps_per_rad = steps_per_circle / (2 * pi);
    return parameters;
}

}  // namespace clipper2next::internal
