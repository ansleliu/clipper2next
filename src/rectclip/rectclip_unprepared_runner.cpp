#include "rectclip/private/rectclip_unprepared_runner.h"

#include "rectclip/private/rectclip_unprepared_bounds.h"
#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_edges.h"
#include "rectclip/private/rectclip_execution_context.h"
#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_path_builder.h"
#include "rectclip/private/rectclip_polygon_executor.h"
#include "rectclip/private/rectclip_thread_state.h"

#include <cstdint>
#include <optional>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto path_coordinates_in_range(const Path64& path) -> bool {
    for (const auto& point : path) {
        if (point.x < MIN_COORD || point.x > MAX_COORD ||
            point.y < MIN_COORD || point.y > MAX_COORD) {
            return false;
        }
    }
    return true;
}

auto reserve_output(Paths64& result, std::size_t reserve_hint) -> void {
    if (result.capacity() == 0U) { result.reserve(reserve_hint); }
}

class unprepared_polygon_clipper final {
public:
    unprepared_polygon_clipper(const Rect64& rect, std::size_t reserve_hint) noexcept
        : rect_{rect}, reserve_hint_{reserve_hint} {}

    auto append(const Path64& path, const Rect64& path_bounds, Paths64& result) -> void {
        if (!context_) {
            context_ = &acquire_reusable_rectclip_context(rect_);
            executor_.emplace(*context_);
            execution_.emplace(*context_);
        }
        context_->path_bounds = path_bounds;
        executor_->execute_path(*execution_, path);
        check_edges(context_->results, context_->edges, context_->rect);
        for (std::size_t edge = 0; edge < 4U; ++edge) {
            tidy_edges(edge,
                       context_->edges[edge * 2U],
                       context_->edges[edge * 2U + 1U],
                       context_->results);
        }
        for (auto& output_ref : context_->results) {
            auto* output_node = output_ref.get();
            auto output_path = build_polygon_path(output_node);
            output_ref = output_node;
            if (!output_path.empty()) {
                reserve_output(result, reserve_hint_);
                result.emplace_back(std::move(output_path));
            }
        }
        context_->reset_polygon_storage();
    }

private:
    Rect64 rect_{};
    std::size_t reserve_hint_{};
    rectclip_context* context_{};
    std::optional<rectclip_polygon_executor> executor_{};
    std::optional<rectclip_execution_context> execution_{};
};

}  // namespace

auto execute_rectclip_unprepared_polygons(
    const Rect64& rect,
    const Paths64& paths,
    bool check_coordinate_range) -> rectclip_unprepared_result {
    rectclip_unprepared_result output;
    if (check_coordinate_range && !rectclip_rect_in_range(rect)) {
        output.in_range = false;
        return output;
    }
    if (rect.is_empty() || paths.empty()) { return output; }

    const bool use_avx2 = rectclip_unprepared_avx2_supported();
    unprepared_polygon_clipper clipper{rect, paths.size()};
    for (const auto& path : paths) {
        if (path.size() < 3U) {
            if (check_coordinate_range && !path_coordinates_in_range(path)) {
                output.paths.clear();
                output.in_range = false;
                return output;
            }
            continue;
        }
        Rect64 path_bounds;
        if (!rectclip_unprepared_path_bounds(
                path, check_coordinate_range, use_avx2, path_bounds)) {
            output.paths.clear();
            output.in_range = false;
            return output;
        }
        if (!rect.intersects(path_bounds)) { continue; }
        if (rect.contains(path_bounds)) {
            reserve_output(output.paths, paths.size());
            output.paths.emplace_back(path);
        } else {
            clipper.append(path, path_bounds, output.paths);
        }
    }
    return output;
}

}  // namespace clipper2next::internal
