#include "rectclip/private/rectclip_thread_state.h"

#include "rectclip/private/rectclip_context.h"

namespace clipper2next::internal {

auto rectclip_context::reset_polygon_storage() -> void {
    op_container.clear();
    results.clear();
    for (auto& edge : edges) { edge.clear(); }
    start_locs.clear();
}

auto rectclip_context::reset_line_storage() -> void {
    results.clear();
    op_container.clear();
    start_locs.clear();
}

auto rectclip_context::reset_for_rect(const Rect64& rect_) -> void {
    rect = rect_;
    rect_as_path = {
        Point64{rect_.left, rect_.top},
        Point64{rect_.right, rect_.top},
        Point64{rect_.right, rect_.bottom},
        Point64{rect_.left, rect_.bottom},
    };
    rect_midpoint = rect_.midpoint();
    path_bounds = {};
    reset_polygon_storage();
}

auto rectclip_context::release() noexcept -> void {
    op_container.release();
    rectclip_node_list{}.swap(results);
    for (auto& edge : edges) { rectclip_node_list{}.swap(edge); }
    std::vector<rect_location>{}.swap(start_locs);
}

namespace {

[[nodiscard]] auto reusable_rectclip_context_storage() -> rectclip_context& {
    thread_local rectclip_context context{Rect64{}};
    return context;
}

}  // namespace

auto acquire_reusable_rectclip_bounds_buffer() -> std::vector<Rect64>& {
    thread_local std::vector<Rect64> path_bounds;
    return path_bounds;
}

auto acquire_reusable_rectclip_context(const Rect64& rect) -> rectclip_context& {
    auto& context = reusable_rectclip_context_storage();
    context.reset_for_rect(rect);
    return context;
}

auto release_rectclip_thread_state() noexcept -> void {
    reusable_rectclip_context_storage().release();
    auto& bounds_buffer = acquire_reusable_rectclip_bounds_buffer();
    std::vector<Rect64>{}.swap(bounds_buffer);
}

}  // namespace clipper2next::internal
