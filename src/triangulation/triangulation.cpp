// SPDX-License-Identifier: BSL-1.0

#include "clipper2next/triangulation/request.h"
#include "api/private/checked_execution.h"

#include "clipper2next/core.h"
#include "clipper2next/geometry/scale.h"
#include "triangulation/private/triangulation_executor.h"
#include "triangulation/private/triangulation_cache.h"
#include "triangulation/private/triangulation_operation.h"
#include "triangulation/private/triangulation_simple.h"

#include "triangulation/private/triangulation_result_builder.h"
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
            if (!coordinate_in_range(point.x) || !coordinate_in_range(point.y)) { return false; }
        }
    }
    return true;
}

template <class Result, class Project>
auto publish_triangulation_graph(internal::triangulation_operation_lease& operation,
                                 const triangulation_request64& request,
                                 TriangulateResult status,
                                 const internal::triangulation_mesh_view& graph,
                                 Result& result, Project&& project) -> void {
    if (result.triangles.capacity() == 0U) {
        // Complete transient buffers before allocating persistent cache data.
        // Publish only after both succeed; supplied buffers keep preparation first.
        decltype(result.triangles) completed;
        project(completed);
        internal::store_cached_triangulation_graph(operation, request, status, graph);
        result.triangles.swap(completed);
    } else {
        internal::store_cached_triangulation_graph(operation, request, status, graph);
        project(result.triangles);
    }
    result.status = status;
}

auto triangulate_into_validated(const triangulation_request64& request,
                                triangulation_result64& result) -> void {
    auto operation = internal::triangulation_operation_lease{};
    if (!request.use_delaunay) {
        auto status = TriangulateResult::fail;
        internal::execute_sweep_triangulation_into(operation, request.paths, status, result.triangles);
        result.status = status;
        return;
    }
    if (internal::try_get_cached_triangulation(operation, request, result)) { return; }
    if (request.paths.size() == 1U && request.paths.front().size() == 4U &&
        internal::try_project_cocircular_quad(operation, request.paths.front(), result.triangles)) {
        result.status = TriangulateResult::success;
        return;
    }
    auto status = TriangulateResult::fail;
    const auto& graph =
        internal::execute_triangulation_graph(operation, request.paths, request.use_delaunay, status);
    publish_triangulation_graph(operation, request, status, graph, result,
        [&](Paths64& output) { internal::build_triangulation_result_into(graph, output); });
}

auto triangulate_scaled_into(Paths64&& paths,
                             bool use_delaunay,
                             double inverse_scale,
                             triangulation_resultd& result) -> void {
    triangulation_request64 scaled_request;
    scaled_request.paths = std::move(paths);
    scaled_request.use_delaunay = use_delaunay;
    auto operation = internal::triangulation_operation_lease{};
    if (!use_delaunay) {
        auto status = TriangulateResult::fail;
        internal::execute_sweep_triangulation_into(operation, scaled_request.paths, status, result.triangles,
                                                   inverse_scale);
        result.status = status;
        return;
    }
    if (internal::try_get_cached_triangulation(operation, scaled_request, result, inverse_scale)) { return; }
    if (scaled_request.paths.size() == 1U && scaled_request.paths.front().size() == 4U &&
        internal::try_project_cocircular_quad(operation, scaled_request.paths.front(),
                                              result.triangles, inverse_scale)) {
        result.status = TriangulateResult::success;
        return;
    }
    auto status = TriangulateResult::fail;
    const auto& graph =
        internal::execute_triangulation_graph(operation, scaled_request.paths, use_delaunay, status);
    publish_triangulation_graph(operation, scaled_request, status, graph, result,
        [&](PathsD& output) {
            internal::build_triangulation_result_into(graph, output, inverse_scale);
        });
}

}  // namespace

auto triangulate_into(const triangulation_request64& request, triangulation_result64& result)
    -> void {
    if (!paths_in_range(request.paths)) { return; }
    triangulate_into_validated(request, result);
}

auto triangulate(const triangulation_request64& request) -> triangulation_result64 {
    triangulation_result64 result;
    triangulate_into(request, result);
    return result;
}

auto triangulate_checked(const triangulation_request64& request)
    -> clipper_result<triangulation_result64> {
    return internal::checked_call<triangulation_result64>(
        [&]() -> clipper_result<triangulation_result64> {
            if (!paths_in_range(request.paths)) {
                return make_clipper_error<triangulation_result64>(
                    clipper_error_code::coordinate_range);
            }
            triangulation_result64 result;
            triangulate_into_validated(request, result);
            return result;
        });
}

auto triangulate_into(const triangulation_requestd& request, triangulation_resultd& result)
    -> void {
    const auto precision = check_precision_range(request.decimal_precision);
    if (!precision.has_value()) { return; }
    const double scale = std::pow(10, precision.value());
    auto scaled_paths = scale_paths<int64_t, double>(request.paths, scale);
    if (!scaled_paths.has_value()) { return; }
    triangulate_scaled_into(
        std::move(scaled_paths.value()), request.use_delaunay, 1.0 / scale, result);
}

auto triangulate(const triangulation_requestd& request) -> triangulation_resultd {
    triangulation_resultd result;
    triangulate_into(request, result);
    return result;
}

auto triangulate_checked(const triangulation_requestd& request)
    -> clipper_result<triangulation_resultd> {
    return internal::checked_call<triangulation_resultd>(
        [&]() -> clipper_result<triangulation_resultd> {
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

            triangulate_scaled_into(
                std::move(scaled_paths.value()), request.use_delaunay, 1.0 / scale, result);
            return result;
        });
}

}  // namespace clipper2next
