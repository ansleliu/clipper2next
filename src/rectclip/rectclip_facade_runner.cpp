#include "rectclip/private/rectclip_facade_runner.h"

#include "clipper2next/api/memory.h"
#include "clipper2next/geometry.h"
#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_edges.h"
#include "rectclip/private/rectclip_execution_context.h"
#include "rectclip/private/rectclip_line_executor.h"
#include "rectclip/private/rectclip_path_builder.h"
#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_polygon_executor.h"
#include "rectclip/private/rectclip_thread_state.h"

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto copy_paths(const Paths64& paths) -> Paths64 {
    Paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        Path64 copied_path;
        copied_path.reserve(path.size());
        copied_path.insert(copied_path.end(), path.begin(), path.end());
        result.emplace_back(std::move(copied_path));
    }
    return result;
}

[[nodiscard]] auto copy_line_paths(const Paths64& paths) -> Paths64 {
    Paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        Path64 copied_path;
        copied_path.reserve(path.size());
        for (const auto& point : path) {
            if (!copied_path.empty() && copied_path.back() == point) { continue; }
            copied_path.emplace_back(point);
        }
        if (copied_path.size() >= 2U) { result.emplace_back(std::move(copied_path)); }
    }
    return result;
}

[[nodiscard]] auto copy_contained_paths(const Paths64& paths,
                                        rectclip_mode mode) -> Paths64 {
    return mode == rectclip_mode::polygons ? copy_paths(paths)
                                           : copy_line_paths(paths);
}

[[nodiscard]] auto execute_polygon_clip(rectclip_context& context,
                                        const Paths64& paths,
                                        std::span<const Rect64> precomputed_bounds) -> Paths64 {
    Paths64 result;
    result.reserve(paths.size());
    rectclip_polygon_executor executor{context};
    rectclip_execution_context execution_context{context};
    const bool use_precomputed_bounds = rectclip_has_precomputed_bounds(paths, precomputed_bounds);

    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& path = paths[index];
        if (path.size() < 3) { continue; }

        context.path_bounds = use_precomputed_bounds ? precomputed_bounds[index] : bounds(path);
        if (!context.rect.intersects(context.path_bounds)) { continue; }
        if (context.rect.contains(context.path_bounds)) {
            result.emplace_back(path);
            continue;
        }

        executor.execute_path(execution_context, path);
        check_edges(context.results, context.edges, context.rect);
        for (std::size_t edge_index = 0; edge_index < 4; ++edge_index) {
            tidy_edges(edge_index,
                       context.edges[edge_index * 2],
                       context.edges[edge_index * 2 + 1],
                       context.results);
        }

        for (auto& output_ref : context.results) {
            auto* output_node = output_ref.get();
            auto output_path = build_polygon_path(output_node);
            output_ref = output_node;
            if (!output_path.empty()) { result.emplace_back(std::move(output_path)); }
        }

        context.reset_polygon_storage();
    }

    return result;
}

[[nodiscard]] auto execute_line_clip(rectclip_context& context,
                                     const Paths64& paths,
                                     std::span<const Rect64> precomputed_bounds) -> Paths64 {
    Paths64 result;
    result.reserve(paths.size());
    rectclip_line_executor executor{context};
    rectclip_execution_context execution_context{context};
    const bool use_precomputed_bounds = rectclip_has_precomputed_bounds(paths, precomputed_bounds);

    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& path = paths[index];
        if (path.size() < 2U) { continue; }
        const auto path_bounds = use_precomputed_bounds ? precomputed_bounds[index] : bounds(path);
        if (!context.rect.intersects(path_bounds)) { continue; }

        executor.execute_path(execution_context, path);
        for (auto& output_ref : context.results) {
            auto* output_node = output_ref.get();
            auto output_path = build_line_path(output_node);
            output_ref = output_node;
            if (!output_path.empty()) { result.emplace_back(std::move(output_path)); }
        }

        context.reset_line_storage();
    }

    return result;
}

}  // namespace

auto execute_rectclip(const Rect64& rect,
                      const Paths64& paths,
                      rectclip_mode mode) -> Paths64 {
    return execute_rectclip(rect, paths, rectclip_path_bounds_view{}, mode);
}

auto execute_rectclip(const Rect64& rect,
                      const Paths64& paths,
                      std::span<const Rect64> path_bounds,
                      rectclip_mode mode) -> Paths64 {
    rectclip_path_bounds_view bounds_view;
    if (rectclip_has_precomputed_bounds(paths, path_bounds)) {
        bounds_view = rectclip_path_bounds_view{
            path_bounds,
            summarize_rectclip_path_bounds(paths, path_bounds),
        };
    }
    return execute_rectclip(rect, paths, bounds_view, mode);
}

auto execute_rectclip(const Rect64& rect,
                      const Paths64& paths,
                      const rectclip_path_bounds_view& path_bounds,
                      rectclip_mode mode) -> Paths64 {
    Paths64 result;
    if (rect.is_empty()) { return result; }
    const bool use_precomputed_bounds = rectclip_has_precomputed_bounds(paths, path_bounds.bounds);
    if (paths.size() == 1U) {
        const auto minimum_size = mode == rectclip_mode::polygons ? 3U : 2U;
        if (paths.front().size() >= minimum_size) {
            const auto single_bounds =
                use_precomputed_bounds ? path_bounds.bounds.front() : bounds(paths.front());
            if (!rect.intersects(single_bounds)) { return result; }
            if (rectclip_rect_contains_bounds(rect, single_bounds, mode)) {
                return copy_contained_paths(paths, mode);
            }
        }
    }
    if (use_precomputed_bounds && path_bounds.summary.has_bounds) {
        const bool all_paths_have_minimum_size =
            mode == rectclip_mode::polygons
                ? path_bounds.summary.all_paths_have_polygon_minimum_size
                : path_bounds.summary.all_paths_have_line_minimum_size;
        if (all_paths_have_minimum_size &&
            rectclip_rect_contains_bounds(rect, path_bounds.summary.combined_bounds, mode)) {
            return copy_contained_paths(paths, mode);
        }
    } else if (!paths.empty()) {
        const auto minimum_size = mode == rectclip_mode::polygons ? 3U : 2U;
        Rect64 combined_bounds;
        if (rectclip_paths_have_minimum_size(paths, minimum_size) &&
            rectclip_paths_bounds_unchecked(paths, combined_bounds) &&
            rectclip_rect_contains_bounds(rect, combined_bounds, mode)) {
            return copy_contained_paths(paths, mode);
        }
    }

    auto& context = acquire_reusable_rectclip_context(rect);
    switch (mode) {
    case rectclip_mode::polygons: {
        return execute_polygon_clip(context, paths, path_bounds.bounds);
    }
    case rectclip_mode::lines: {
        return execute_line_clip(context, paths, path_bounds.bounds);
    }
    }

    return result;
}

auto copy_rectclip_paths(const Paths64& paths) -> Paths64 {
    return copy_paths(paths);
}

}  // namespace clipper2next::internal
