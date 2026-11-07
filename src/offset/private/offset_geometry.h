#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <span>

#include "geometry/private/numeric_policy.h"
#include "support/private/checked_size.h"
#include "clipper2next/api/error.h"
#include "clipper2next/offset/types.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

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

// Maximum points at a vertex includes the concave three-point bridge.
[[nodiscard]] inline auto offset_join_point_bound(JoinType join, const offset_arc_parameters& arc) noexcept -> std::size_t {
    if (join != JoinType::Round) { return 3U; }
    const auto steps = arc_step_count(arc, pi);
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    return steps == maximum ? maximum : std::max(std::size_t{3U}, steps + 1U);
}
[[nodiscard]] inline auto offset_circle_steps(const offset_arc_parameters& arc, double delta) noexcept -> std::size_t {
    const auto steps = arc_step_count(arc, 2 * pi);
    if (steps > 2U || std::abs(delta) < 1.0) { return steps; }
    const auto automatic = pi * std::sqrt(std::abs(delta));
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    return !std::isfinite(automatic) || automatic >= static_cast<double>(maximum)
        ? maximum : static_cast<std::size_t>(automatic);
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

#if defined(__x86_64__) || defined(_M_X64)
[[nodiscard]] inline auto offset_sse41_available() noexcept -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<bool>(__builtin_cpu_supports("sse4.1"));
#elif defined(_MSC_VER)
    static const bool available = [] {
        int registers[4];
        __cpuid(registers, 1);
        return (registers[2] & (1 << 19)) != 0;
    }();
    return available;
#else
    return false;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse4.1"), noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
[[nodiscard]] inline auto round_offset_pair_sse41(const PointD& point) -> PointD {
    const auto values = _mm_set_pd(point.y, point.x);
#if defined(_MSC_VER)
    constexpr auto floor_mode = _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC;
#else
    constexpr auto floor_mode = _MM_FROUND_TO_NEG_INF;
#endif
    const auto lower = _mm_round_pd(values, floor_mode);
    const auto fraction = _mm_sub_pd(values, lower);
    if (_mm_movemask_pd(_mm_cmpeq_pd(fraction, _mm_set1_pd(0.5))) != 0) {
        return {geotypes::roundToEven(point.x), geotypes::roundToEven(point.y)};
    }
    const auto increment = _mm_and_pd(
        _mm_cmpgt_pd(fraction, _mm_set1_pd(0.5)), _mm_set1_pd(1.0));
    const auto rounded = _mm_add_pd(lower, increment);
    return {_mm_cvtsd_f64(rounded), _mm_cvtsd_f64(_mm_unpackhi_pd(rounded, rounded))};
}
#endif

[[nodiscard]] inline auto rounded_offset_point(const PointD& point,
                                               geotypes::CoordinateRounding rounding) -> PointD {
#if defined(__x86_64__) || defined(_M_X64)
    constexpr auto guarded = _MM_ROUND_MASK | _MM_MASK_MASK | 0x8000U;
    if (rounding == geotypes::CoordinateRounding::NearestEven &&
        (_mm_getcsr() & guarded) == _MM_MASK_MASK && offset_sse41_available()) {
        return round_offset_pair_sse41(point);
    }
#endif
    return {geotypes::roundToNearest(point.x, rounding),
            geotypes::roundToNearest(point.y, rounding)};
}

class offset_point_rounding final {
public:
    explicit constexpr offset_point_rounding(
        geotypes::CoordinateRounding mode = geotypes::CoordinateRounding::NearestEven) noexcept
        : mode_(mode) {}

    // The caller bounds every write and excludes callbacks for this phase.
    // No output allocation may occur after this snapshot is taken.
    [[nodiscard]] static auto for_reserved_phase(
        geotypes::CoordinateRounding mode, const Path64& output,
        std::size_t maximum_points) noexcept -> offset_point_rounding {
        auto policy = offset_point_rounding{mode};
        if (output.size() > maximum_points || output.capacity() < maximum_points) { return policy; }
        if (mode == geotypes::CoordinateRounding::NearestAwayFromZero) {
            policy.backend_ = backend::scalar;
        }
#if defined(__x86_64__) || defined(_M_X64)
        if (mode == geotypes::CoordinateRounding::NearestEven) {
            constexpr auto controls = _MM_ROUND_MASK | _MM_MASK_MASK | 0x8000U;
            const auto environment = _mm_getcsr();
            if ((environment & controls) == _MM_MASK_MASK) {
                policy.backend_ = (environment & _MM_EXCEPT_INEXACT) != 0U
                    ? backend::nearest_integer
                    : offset_sse41_available() ? backend::paired_floor : backend::scalar;
            }
        }
#endif
        return policy;
    }

    [[nodiscard]] auto mode() const noexcept -> geotypes::CoordinateRounding { return mode_; }
    [[nodiscard]] auto uses_nearest_integer() const noexcept -> bool {
#if defined(__x86_64__) || defined(_M_X64)
        if (backend_ == backend::nearest_integer) { return true; }
        if (backend_ == backend::dynamic) {
            constexpr auto guard = _MM_ROUND_MASK | _MM_MASK_MASK | 0x8000U | _MM_EXCEPT_INEXACT;
            constexpr auto required = _MM_MASK_MASK | _MM_EXCEPT_INEXACT;
            return (_mm_getcsr() & guard) == required;
        }
#endif
        return false;
    }
    [[nodiscard]] auto round(const PointD& point) const -> PointD {
        if (backend_ == backend::scalar) {
            return {geotypes::roundToNearest(point.x, mode_),
                    geotypes::roundToNearest(point.y, mode_)};
        }
#if defined(__x86_64__) || defined(_M_X64)
        if (backend_ == backend::paired_floor) { return round_offset_pair_sse41(point); }
#endif
        return rounded_offset_point(point, mode_);
    }

private:
    enum class backend { dynamic, nearest_integer, paired_floor, scalar };
    geotypes::CoordinateRounding mode_;
    backend backend_{backend::dynamic};
};

// Floating offset vertices cross this boundary before becoming integer output.
// Numeric failures here describe generation, not user input.
#if defined(_MSC_VER)
#define CLIPPER2NEXT_OFFSET_POINT_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define CLIPPER2NEXT_OFFSET_POINT_INLINE inline __attribute__((always_inline))
#else
#define CLIPPER2NEXT_OFFSET_POINT_INLINE inline
#endif

[[nodiscard]] CLIPPER2NEXT_OFFSET_POINT_INLINE auto offset_point(const PointD& point,
                                      const offset_point_rounding& policy) -> Point64 {
    const auto rounding = policy.mode();
    if ((rounding != geotypes::CoordinateRounding::NearestEven &&
         rounding != geotypes::CoordinateRounding::NearestAwayFromZero) ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) {
        raise_clipper_error(clipper_error_code::coordinate_range);
    }
#if defined(__x86_64__) || defined(_M_X64)
    constexpr auto working_lower = static_cast<double>(MIN_COORD - 1);
    constexpr auto working_upper = static_cast<double>(MAX_COORD + 1);
    if (rounding == geotypes::CoordinateRounding::NearestEven &&
        policy.uses_nearest_integer() &&
        point.x > working_lower && point.x < working_upper &&
        point.y > working_lower && point.y < working_upper) {
        return {_mm_cvtsd_si64(_mm_set_sd(point.x)),
                _mm_cvtsd_si64(_mm_set_sd(point.y))};
    }
#endif
    const auto rounded = policy.round(point);
    // These exclusive power-of-two bounds are exact in double. Every admitted
    // rounded coordinate satisfies both the working and integer domains.
    constexpr auto lower = static_cast<double>(MIN_COORD - 1);
    constexpr auto upper = static_cast<double>(MAX_COORD + 1);
    if (rounded.x <= lower || rounded.x >= upper ||
        rounded.y <= lower || rounded.y >= upper) {
        raise_clipper_error(clipper_error_code::coordinate_range);
    }
    return {static_cast<std::int64_t>(rounded.x), static_cast<std::int64_t>(rounded.y)};
}

#undef CLIPPER2NEXT_OFFSET_POINT_INLINE

[[nodiscard]] inline auto perpendicular_point(const Point64& point,
                                              const PointD& normal,
                                              double delta,
                                              const offset_point_rounding& rounding = offset_point_rounding{})
    -> Point64 {
    return offset_point({point.x + normal.x * delta, point.y + normal.y * delta}, rounding);
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

// Storage owners construct the complete output array before filling its values.
inline auto fill_normals(std::span<PointD> normals, std::span<const Point64> path) -> void {
    assert(normals.size() == path.size());
    if (path.empty()) { return; }

    auto normal = normals.begin();
    for (auto path_iter = path.cbegin(); path_iter != std::prev(path.cend()); ++path_iter) {
        *normal++ = internal::unit_normal(*path_iter, *(path_iter + 1));
    }
    normals.back() = internal::unit_normal(path.back(), path.front());
}

[[nodiscard]] inline auto build_normals(std::span<const Point64> path) -> PathD {
    static_cast<void>(checked_size_multiply(path.size(), sizeof(PointD)));
    PathD normals;
    normals.reserve(path.size());
    normals.resize(path.size());
    internal::fill_normals(normals, path);
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
