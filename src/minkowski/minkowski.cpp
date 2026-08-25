#include "clipper2next/minkowski/request.h"

#include "clipper2next/geometry.h"
#include "clipper2next/geometry/scale.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "minkowski/private/minkowski.h"

namespace clipper2next {
namespace {

template <typename Paths>
[[nodiscard]] auto empty_minkowski_result() -> Paths {
    return {};
}

[[nodiscard]] auto coordinate_in_range(std::int64_t value) -> bool {
    return value >= MIN_COORD && value <= MAX_COORD;
}

[[nodiscard]] auto path_in_range(const Path64& path) -> bool {
    return std::all_of(path.begin(), path.end(), [](const Point64& point) {
        return coordinate_in_range(point.x) && coordinate_in_range(point.y);
    });
}

[[nodiscard]] auto translated_coordinate_in_range(std::int64_t path_coordinate,
                                                  std::int64_t pattern_coordinate,
                                                  bool is_sum) noexcept -> bool {
    if (is_sum) {
        return pattern_coordinate >= 0
                   ? path_coordinate <= MAX_COORD - pattern_coordinate
                   : path_coordinate >= MIN_COORD - pattern_coordinate;
    }
    return pattern_coordinate >= 0
               ? path_coordinate >= MIN_COORD + pattern_coordinate
               : path_coordinate <= MAX_COORD + pattern_coordinate;
}

[[nodiscard]] auto minkowski_translation_in_range(const Path64& pattern,
                                                  const Path64& path,
                                                  bool is_sum) -> bool {
    for (const auto& path_point : path) {
        for (const auto& pattern_point : pattern) {
            if (!translated_coordinate_in_range(path_point.x, pattern_point.x, is_sum) ||
                !translated_coordinate_in_range(path_point.y, pattern_point.y, is_sum)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto minkowski_request_in_range(const minkowski_request64& request,
                                              bool is_sum) -> bool {
    return path_in_range(request.pattern) && path_in_range(request.path) &&
           minkowski_translation_in_range(request.pattern, request.path, is_sum);
}

[[nodiscard]] auto execute_minkowski64_validated(const minkowski_request64& request,
                                                 bool is_sum) -> Paths64 {
    return internal::union_minkowski_quads(
        internal::build_minkowski_quads(request.pattern,
                                        request.path,
                                        is_sum,
                                        request.is_closed),
        FillRule::NonZero);
}

}  // namespace

auto minkowski_sum(const minkowski_request64& request) -> Paths64 {
    if (!minkowski_request_in_range(request, true)) {
        return empty_minkowski_result<Paths64>();
    }
    return execute_minkowski64_validated(request, true);
}

auto minkowski_sum_checked(const minkowski_request64& request) -> clipper_result<Paths64> {
    if (!minkowski_request_in_range(request, true)) {
        return make_clipper_error<Paths64>(clipper_error_code::coordinate_range);
    }
    return execute_minkowski64_validated(request, true);
}

auto minkowski_sum(const minkowski_requestd& request) -> PathsD {
    const auto precision = check_precision_range(request.decimal_precision);
    if (!precision.has_value()) { return empty_minkowski_result<PathsD>(); }
    const double scale = std::pow(10, precision.value());
    auto scaled_pattern = scale_path<int64_t, double>(request.pattern, scale);
    auto scaled_path = scale_path<int64_t, double>(request.path, scale);
    if (!scaled_pattern.has_value() || !scaled_path.has_value()) {
        return empty_minkowski_result<PathsD>();
    }
    if (!minkowski_translation_in_range(scaled_pattern.value(), scaled_path.value(), true)) {
        return empty_minkowski_result<PathsD>();
    }
    auto quads = internal::build_minkowski_quads(
        scaled_pattern.value(),
        scaled_path.value(),
        true,
        request.is_closed);
    const Paths64 unioned =
        internal::union_minkowski_quads(std::move(quads), FillRule::NonZero);
    auto unscaled = scale_paths<double, int64_t>(unioned, 1 / scale);
    return unscaled.has_value() ? std::move(unscaled.value())
                                : empty_minkowski_result<PathsD>();
}

auto minkowski_sum_checked(const minkowski_requestd& request) -> clipper_result<PathsD> {
    const auto precision = check_precision_range(request.decimal_precision);
    if (!precision.has_value()) { return make_clipper_error<PathsD>(precision.error()); }
    const double scale = std::pow(10, precision.value());
    auto scaled_pattern = scale_path<int64_t, double>(request.pattern, scale);
    if (!scaled_pattern.has_value()) { return make_clipper_error<PathsD>(scaled_pattern.error()); }
    auto scaled_path = scale_path<int64_t, double>(request.path, scale);
    if (!scaled_path.has_value()) { return make_clipper_error<PathsD>(scaled_path.error()); }
    if (!minkowski_translation_in_range(scaled_pattern.value(), scaled_path.value(), true)) {
        return make_clipper_error<PathsD>(clipper_error_code::coordinate_range);
    }
    auto quads = internal::build_minkowski_quads(
        scaled_pattern.value(),
        scaled_path.value(),
        true,
        request.is_closed);
    const Paths64 unioned =
        internal::union_minkowski_quads(std::move(quads), FillRule::NonZero);
    auto unscaled = scale_paths<double, int64_t>(unioned, 1 / scale);
    if (!unscaled.has_value()) { return make_clipper_error<PathsD>(unscaled.error()); }
    // cppcheck-suppress returnStdMoveLocal
    return std::move(unscaled.value());
}

auto minkowski_difference(const minkowski_request64& request) -> Paths64 {
    if (!minkowski_request_in_range(request, false)) {
        return empty_minkowski_result<Paths64>();
    }
    return execute_minkowski64_validated(request, false);
}

auto minkowski_difference_checked(const minkowski_request64& request)
    -> clipper_result<Paths64> {
    if (!minkowski_request_in_range(request, false)) {
        return make_clipper_error<Paths64>(clipper_error_code::coordinate_range);
    }
    return execute_minkowski64_validated(request, false);
}

auto minkowski_difference(const minkowski_requestd& request) -> PathsD {
    const auto precision = check_precision_range(request.decimal_precision);
    if (!precision.has_value()) { return empty_minkowski_result<PathsD>(); }
    const double scale = std::pow(10, precision.value());
    auto scaled_pattern = scale_path<int64_t, double>(request.pattern, scale);
    auto scaled_path = scale_path<int64_t, double>(request.path, scale);
    if (!scaled_pattern.has_value() || !scaled_path.has_value()) {
        return empty_minkowski_result<PathsD>();
    }
    if (!minkowski_translation_in_range(scaled_pattern.value(), scaled_path.value(), false)) {
        return empty_minkowski_result<PathsD>();
    }
    auto quads = internal::build_minkowski_quads(
        scaled_pattern.value(),
        scaled_path.value(),
        false,
        request.is_closed);
    const Paths64 unioned =
        internal::union_minkowski_quads(std::move(quads), FillRule::NonZero);
    auto unscaled = scale_paths<double, int64_t>(unioned, 1 / scale);
    return unscaled.has_value() ? std::move(unscaled.value())
                                : empty_minkowski_result<PathsD>();
}

auto minkowski_difference_checked(const minkowski_requestd& request) -> clipper_result<PathsD> {
    const auto precision = check_precision_range(request.decimal_precision);
    if (!precision.has_value()) { return make_clipper_error<PathsD>(precision.error()); }
    const double scale = std::pow(10, precision.value());
    auto scaled_pattern = scale_path<int64_t, double>(request.pattern, scale);
    if (!scaled_pattern.has_value()) { return make_clipper_error<PathsD>(scaled_pattern.error()); }
    auto scaled_path = scale_path<int64_t, double>(request.path, scale);
    if (!scaled_path.has_value()) { return make_clipper_error<PathsD>(scaled_path.error()); }
    if (!minkowski_translation_in_range(scaled_pattern.value(), scaled_path.value(), false)) {
        return make_clipper_error<PathsD>(clipper_error_code::coordinate_range);
    }
    auto quads = internal::build_minkowski_quads(
        scaled_pattern.value(),
        scaled_path.value(),
        false,
        request.is_closed);
    const Paths64 unioned =
        internal::union_minkowski_quads(std::move(quads), FillRule::NonZero);
    auto unscaled = scale_paths<double, int64_t>(unioned, 1 / scale);
    if (!unscaled.has_value()) { return make_clipper_error<PathsD>(unscaled.error()); }
    // cppcheck-suppress returnStdMoveLocal
    return std::move(unscaled.value());
}

}  // namespace clipper2next
