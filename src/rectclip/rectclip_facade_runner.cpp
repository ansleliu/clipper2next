#include "rectclip/private/rectclip_facade_runner.h"

#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_edges.h"
#include "rectclip/private/rectclip_execution_context.h"
#include "rectclip/private/rectclip_line_executor.h"
#include "rectclip/private/rectclip_path_builder.h"
#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_polygon_executor.h"

#include "support/private/paths_rebuild.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <utility>

namespace clipper2next::internal {
namespace {

template <typename BoundsAt>
auto build_rectclip_polygon_paths(rectclip_context& context,
                               const Paths64& paths,
                               BoundsAt bounds_at) -> Paths64 {
    const auto& rect = context.rect;
    if (rect.is_empty()) { return {}; }
    Paths64 output;
    output.reserve(paths.size());
    rectclip_execution_context execution{context};
    rectclip_polygon_executor executor{context};
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto& path = paths[i];
        if (path.size() < 3U) { continue; }
        bounds_at(i, path, context.path_bounds);
        if (!rect.intersects(context.path_bounds)) { continue; }
        if (rect.contains(context.path_bounds)) {
            output.push_back(path);
            continue;
        }
        executor.execute_path(execution, path);
        auto needs_cleanup = check_edges(context.results, context.edges, context.rect);
        for (std::size_t edge = 0; edge < 4; ++edge) {
            needs_cleanup |= tidy_edges(
                edge, context.edges[edge * 2], context.edges[edge * 2 + 1], context.results);
        }
        for (auto ref : context.results) {
            auto* node = ref.get();
            // An unchanged checked ring already has no collinear corners.
            // Link edits invalidate that proof and keep the established pass.
            if (needs_cleanup) { prepare_polygon_path(node); }
            if (node) { append_materialized_rectclip_path(node, output); }
        }
        context.reset_polygon_storage();
    }
    return output;
}

}  // namespace

// Each generated contour belongs to the private result before publication.
auto build_rectclip_paths(rectclip_context& context,
                          const Paths64& paths,
                          std::span<const Rect64> precomputed_bounds,
                          rectclip_mode mode) -> Paths64 {
    assert(precomputed_bounds.size() == paths.size());
    if (mode != rectclip_mode::polygons) {
        return build_rectclip_line_paths(context.rect, paths, precomputed_bounds);
    }
    const auto bounds_at = [precomputed_bounds](std::size_t index, const Path64&, Rect64& bounds) {
        bounds = precomputed_bounds[index];
    };
    return build_rectclip_polygon_paths(context, paths, bounds_at);
}

auto build_rectclip_line_paths(const Rect64 rect, const Paths64& paths,
                               std::span<const Rect64> precomputed_bounds) -> Paths64 {
    assert(precomputed_bounds.size() == paths.size());
    if (rect.is_empty()) { return {}; }
    Paths64 output;
    output.reserve(paths.size());
    rectclip_line_executor executor{rect};
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto& path = paths[i];
        if (path.size() < 2U) { continue; }
        const auto& bounds = precomputed_bounds[i];
        if (!rect.intersects(bounds)) { continue; }
        if (rectclip_rect_strictly_contains_bounds(rect, bounds)) {
            auto& line = output.emplace_back(path);
            line.erase(std::unique(line.begin(), line.end()), line.end());
            if (line.size() < 2U) { output.pop_back(); }
        } else { executor.execute_path(output, path); }
    }
    return output;
}

auto build_rectclip_unprepared_polygon_paths(rectclip_context& context,
                                            const Paths64& paths) -> Paths64 {
    return build_rectclip_polygon_paths(
        context, paths, [](std::size_t, const Path64& path, Rect64& bounds) {
            static_cast<void>(rectclip_path_bounds(path, false, bounds));
        });
}

auto execute_rectclip_into(rectclip_context& context,
                           const Paths64& paths,
                           std::span<const Rect64> precomputed_bounds,
                           rectclip_mode mode,
                           Paths64& destination) -> void {
    auto completed = build_rectclip_paths(context, paths, precomputed_bounds, mode);
    // All input has been consumed. Preserve existing path buffers and the
    // established prepare/begin/failure contract at the supplied-output boundary.
    materialize_paths(std::move(completed), destination);
}

auto execute_rectclip_lines_into(const Rect64 rect, const Paths64& paths,
                                 std::span<const Rect64> precomputed_bounds,
                                 Paths64& destination) -> void {
    auto completed = build_rectclip_line_paths(rect, paths, precomputed_bounds);
    materialize_paths(std::move(completed), destination);
}

}  // namespace clipper2next::internal
