// SPDX-License-Identifier: BSL-1.0

#include "clipper2next/triangulation/request.h"

#include "clipper2next/core.h"
#include "clipper2next/geometry/scale.h"
#include "triangulation/private/triangulation_executor.h"
#include "triangulation/private/triangulation_cache.h"

#include <cmath>
#include <cstdint>

namespace clipper2next {
namespace {

[[nodiscard]] auto coordinate_in_range(std::int64_t value) -> bool {
    return value >= MIN_COORD && value <= MAX_COORD;
}

[[nodiscard]] auto paths_in_range(const Paths64& paths) -> bool {
    for (const auto& path : paths) {
        for (const auto& point : path) {
            if (!coordinate_in_range(point.x) || !coordinate_in_range(point.y)) {
                return false;
            }
        }
    }
    return true;
}

auto triangulate_into_validated(const triangulation_request64& request,
                                triangulation_result64& result) -> void {
    if (internal::try_get_cached_triangulation(request, result)) { return; }
    result.status = TriangulateResult::fail;
    result.triangles =
        internal::execute_triangulation(request.paths,
                                        request.use_delaunay,
                                        result.status);
    internal::store_cached_triangulation(request, result);
}

}  // namespace

namespace internal {

auto release_triangulation_thread_state() noexcept -> void {
    release_triangulation_execution_thread_state();
    release_triangulation_cache();
}

}  // namespace internal

auto triangulate_into(const triangulation_request64& request, triangulation_result64& result)
    -> void {
    if (!paths_in_range(request.paths)) {
        result.status = TriangulateResult::fail;
        result.triangles.clear();
        return;
    }
    triangulate_into_validated(request, result);
}

auto triangulate(const triangulation_request64& request) -> triangulation_result64 {
    triangulation_result64 result;
    triangulate_into(request, result);
    return result;
}

auto triangulate_checked(const triangulation_request64& request)
    -> clipper_result<triangulation_result64> {
    if (!paths_in_range(request.paths)) {
        return make_clipper_error<triangulation_result64>(clipper_error_code::coordinate_range);
    }
    triangulation_result64 result;
    triangulate_into_validated(request, result);
    return result;
}

auto triangulate_into(const triangulation_requestd& request, triangulation_resultd& result)
    -> void {
    result.status = TriangulateResult::fail;
    result.triangles.clear();

    const auto precision = check_precision_range(request.decimal_precision);
    if (!precision.has_value()) { return; }

    // Negative precision scales down, matching the minkowski/offset D-suffix
    // entry points (pow semantics) rather than silently clamping to 1.0.
    const double scale = std::pow(10, precision.value());

    auto scaled_paths = scale_paths<int64_t, double>(request.paths, scale);
    if (!scaled_paths.has_value()) { return; }

    const auto scaled_solution =
        internal::execute_triangulation(scaled_paths.value(),
                                        request.use_delaunay,
                                        result.status);
    auto unscaled = scale_paths<double, int64_t>(scaled_solution, 1 / scale);
    if (!unscaled.has_value()) {
        result.status = TriangulateResult::fail;
        return;
    }
    result.triangles = std::move(unscaled.value());
}

auto triangulate(const triangulation_requestd& request) -> triangulation_resultd {
    triangulation_resultd result;
    triangulate_into(request, result);
    return result;
}

auto triangulate_checked(const triangulation_requestd& request)
    -> clipper_result<triangulation_resultd> {
    triangulation_resultd result;
    const auto precision = check_precision_range(request.decimal_precision);
    if (!precision.has_value()) {
        return make_clipper_error<triangulation_resultd>(precision.error());
    }

    const double scale = std::pow(10, precision.value());
    auto scaled_paths = scale_paths<int64_t, double>(request.paths, scale);
    if (!scaled_paths.has_value()) {
        return make_clipper_error<triangulation_resultd>(scaled_paths.error());
    }

    triangulation_request64 scaled_request;
    scaled_request.paths = std::move(scaled_paths.value());
    scaled_request.use_delaunay = request.use_delaunay;
    auto scaled_result = triangulate(scaled_request);
    auto unscaled = scale_paths<double, int64_t>(scaled_result.triangles, 1 / scale);
    if (!unscaled.has_value()) {
        return make_clipper_error<triangulation_resultd>(unscaled.error());
    }
    result.status = scaled_result.status;
    result.triangles = std::move(unscaled.value());
    return result;
}

}  // namespace clipper2next
