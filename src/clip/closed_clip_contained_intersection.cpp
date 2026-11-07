#include "clip/private/closed_clip_fast_path.h"

#include "clipper2next/geometry.h"
#include "clipper2next/geometry/predicates.h"
#include "clipper2next/core/rect.h"
#include "geometry/private/path_simplicity.h"

#include "support/private/paths_rebuild.h"
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace {

[[nodiscard]] constexpr auto relaxed_contained_intersection_point_threshold() noexcept
    -> std::size_t {
    return 256U;
}

// Upper bound for the quadratic self-intersection scan on non-convex
// passthrough candidates; larger paths fall back to the full engine.
[[nodiscard]] constexpr auto contained_intersection_simplicity_scan_limit() noexcept
    -> std::size_t {
    return 512U;
}

[[nodiscard]] auto rectangle_strictly_contains_point(const Rect64& rect, const Point64& point)
    -> bool {
    return point.x > rect.left && point.x < rect.right && point.y > rect.top &&
           point.y < rect.bottom;
}

[[nodiscard]] auto points_are_really_close(const Point64& first, const Point64& second) -> bool {
    return std::llabs(first.x - second.x) < 2 && std::llabs(first.y - second.y) < 2;
}

[[nodiscard]] auto contained_path_passthrough_size(const Path64& path) -> std::size_t {
    auto size = path.size();
    while (size > 1U && path.front() == path[size - 1U]) { --size; }
    return size;
}

[[nodiscard]] auto is_very_small_passthrough_triangle(const Path64& path) -> bool {
    return path.size() == 3U &&
           (points_are_really_close(path[0], path[1]) ||
            points_are_really_close(path[1], path[2]) || points_are_really_close(path[2], path[0]));
}

[[nodiscard]] auto should_remove_passthrough_point(const Point64& previous,
                                                   const Point64& current,
                                                   const Point64& next) -> bool {
    return is_collinear(previous, current, next) &&
           (current == previous || current == next || dot_product(previous, current, next) < 0);
}

[[nodiscard]] auto copy_contained_path_for_passthrough(const Path64& source,
                                                       std::size_t path_size)
    -> Path64 {
    return Path64{source.begin(),
                  source.begin() + static_cast<std::ptrdiff_t>(path_size)};
}

[[nodiscard]] auto clean_contained_path_for_passthrough(const Path64& source,
                                                        std::size_t path_size)
    -> std::optional<Path64> {
    auto path = copy_contained_path_for_passthrough(source, path_size);
    for (std::size_t index = 0; path.size() >= 3U && index < path.size();) {
        const auto previous_index = index == 0U ? path.size() - 1U : index - 1U;
        const auto next_index = (index + 1U) % path.size();
        if (!should_remove_passthrough_point(path[previous_index], path[index], path[next_index])) {
            ++index;
            continue;
        }

        path.erase(path.begin() + static_cast<std::ptrdiff_t>(index));
        if (index > 0U) { --index; }
    }

    if (path.size() < 3U || is_very_small_passthrough_triangle(path)) { return std::nullopt; }
    return path;
}

struct contained_contour {
    Path64 points;
    Rect64 bounds;
    double signed_area;
};

[[nodiscard]] auto orient_contours(std::vector<contained_contour>& contours, FillRule fill_rule)
    -> bool {
    std::vector<std::size_t> depths(contours.size(), 0U);
    for (const auto& contour : contours) {
        if (contour.signed_area == 0.0) { return false; }
    }
    for (std::size_t i = 0; i < contours.size(); ++i) {
        for (std::size_t j = i + 1; j < contours.size(); ++j) {
            const auto& first = contours[i];
            const auto& second = contours[j];
            if (!first.bounds.intersects(second.bounds)) { continue; }
            auto relation = [&](const auto& outer, const auto& inner) {
                return outer.bounds.contains(inner.bounds)
                           ? point_in_polygon(inner.points.front(), outer.points)
                           : PointInPolygonResult::IsOutside;
            };
            const auto first_contains = relation(first, second);
            const auto second_contains = relation(second, first);
            if (first_contains == PointInPolygonResult::IsOn ||
                second_contains == PointInPolygonResult::IsOn ||
                first_contains == second_contains) {
                return false;
            }
            ++depths[first_contains == PointInPolygonResult::IsInside ? j : i];
        }
    }
    for (std::size_t i = 0; i < contours.size(); ++i) {
        if ((contours[i].signed_area > 0.0) != ((depths[i] % 2) == 0)) {
            if (contours.size() != 1U && fill_rule != FillRule::EvenOdd) { return false; }
            std::reverse(contours[i].points.begin(), contours[i].points.end());
        }
    }
    return true;
}

}  // namespace

auto contained_intersection_fast_path_point_threshold() noexcept -> std::size_t {
    return 4096U;
}

auto try_execute_disjoint_bounds_intersection(const clip_request64& request,
                                              const clip_request_metadata64& metadata,
                                              paths64_result& result) -> bool {
    if (request.clip_type != ClipType::Intersection ||
        metadata.open_subject_path_count != 0U) {
        return false;
    }
    if (metadata.subject_path_count == 0U || metadata.clip_path_count == 0U) {
        result.closed.clear();
        result.open.clear();
        return true;
    }

    const auto subject_bounds = bounds(request.subjects);
    const auto clip_bounds = bounds(request.clips);
    const bool has_positive_overlap =
        (std::max)(subject_bounds.left, clip_bounds.left) <
            (std::min)(subject_bounds.right, clip_bounds.right) &&
        (std::max)(subject_bounds.top, clip_bounds.top) <
            (std::min)(subject_bounds.bottom, clip_bounds.bottom);
    if (has_positive_overlap) { return false; }

    result.closed.clear();
    result.open.clear();
    return true;
}

auto try_execute_containing_rectangle_intersection(const clip_request64& request,
                                                   const clip_request_metadata64& metadata,
                                                   paths64_result& result) -> bool {
    if (request.clip_type != ClipType::Intersection || metadata.clip_path_count != 1U ||
        metadata.subject_path_count == 0U || !metadata.single_clip_rect) {
        return false;
    }

    std::vector<contained_contour> passthrough;
    passthrough.reserve(request.subjects.size());
    std::size_t total_points = 0;
    const auto point_threshold = contained_intersection_fast_path_point_threshold();
    for (const auto& path : request.subjects) {
        if (path.size() > point_threshold - total_points) { return false; }
        total_points += path.size();

        const auto path_size = contained_path_passthrough_size(path);
        if (path_size < 3U) { return false; }

        auto needs_cleanup = false;
        for (std::size_t index = 0; index < path_size; ++index) {
            const auto& current = path[index];
            if (!rectangle_strictly_contains_point(*metadata.single_clip_rect, current)) {
                return false;
            }
            const auto& previous = path[index == 0U ? path_size - 1U : index - 1U];
            const auto& next = path[(index + 1U) % path_size];
            if (path_size <= relaxed_contained_intersection_point_threshold()) {
                if (points_are_really_close(current, next) ||
                    is_collinear(previous, current, next)) {
                    return false;
                }
            } else if (should_remove_passthrough_point(previous, current, next)) {
                needs_cleanup = true;
            }
        }

        auto next_path =
            needs_cleanup
                ? clean_contained_path_for_passthrough(path, path_size)
                : std::optional<Path64>{
                      copy_contained_path_for_passthrough(path, path_size)};
        if (!next_path) { return false; }
        // The engine resolves self-intersections (a bowtie becomes two
        // triangles); passthrough must only ever emit provably simple rings.
        if (!path_simplicity::path_is_provably_simple(
                *next_path, contained_intersection_simplicity_scan_limit())) {
            return false;
        }
        const auto path_bounds = bounds(*next_path);
        const auto signed_area = area(*next_path);
        passthrough.push_back({std::move(*next_path), path_bounds, signed_area});
    }

    if (!orient_contours(passthrough, request.fill_rule)) { return false; }
    paths_rebuild<int64_t> transaction{result.closed};
    transaction.prepare(passthrough.size(), [&](auto i) { return passthrough[i].points.size(); });
    transaction.begin();
    for (std::size_t i = 0; i < passthrough.size(); ++i) {
        const auto& path = passthrough[i].points;
        result.closed[i].insert(result.closed[i].end(), path.begin(), path.end());
    }
    transaction.finish();
    result.open.clear();
    return true;
}

}  // namespace clipper2next::internal
