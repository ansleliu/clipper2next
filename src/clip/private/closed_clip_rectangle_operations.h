#pragma once

#include "clipper2next/api/result.h"
#include "clipper2next/core/rect.h"
#include "support/private/paths_rebuild.h"

#include <array>
#include <optional>
#include <span>

namespace clipper2next::internal {

// Rectangle relations bound the answer to at most eight vertices per ring.
// Keep geometry parameters, not a temporary final point buffer; only the
// materializer expands this layout into vertices after destination preparation.
enum class rectangle_x { first_left, first_right, second_left, second_right };
enum class rectangle_y { first_top, first_bottom, second_top, second_bottom };
struct rectangle_vertex {
    rectangle_x x;
    rectangle_y y;
};
struct rectangle_path_layout final {
    Rect64 first{};
    Rect64 second{};
    std::span<const rectangle_vertex> vertices{};
    [[nodiscard]] auto point_count() const noexcept -> std::size_t { return vertices.size(); }
};

[[nodiscard]] auto rectangles_have_strict_overlap(const Rect64&, const Rect64&) -> bool;
[[nodiscard]] auto rectangles_share_edge_segment(const Rect64&, const Rect64&) -> bool;
[[nodiscard]] auto strict_intersection_rect(const Rect64&, const Rect64&) -> std::optional<Rect64>;
[[nodiscard]] auto rectangle_clip_solution_layout(const Rect64&) -> rectangle_path_layout;
[[nodiscard]] auto try_layout_rectangular_union(const Rect64&,
                                                const Rect64&,
                                                rectangle_path_layout&) -> bool;
[[nodiscard]] auto try_layout_two_rectangle_union(const Rect64&,
                                                  const Rect64&,
                                                  rectangle_path_layout&) -> bool;
[[nodiscard]] auto try_layout_rectangle_corner_difference(const Rect64&,
                                                          const Rect64&,
                                                          rectangle_path_layout&) -> bool;
template <std::size_t Count>
auto materialize_rectangle_paths(
    paths64_result& result,
    const std::array<rectangle_path_layout, Count>& layouts) -> void {
    paths_rebuild<int64_t> transaction{result.closed};
    transaction.prepare(Count, [&](auto i) { return layouts[i].point_count(); });
    transaction.begin();
    for (std::size_t i = 0; i < Count; ++i) {
        const auto& layout = layouts[i];
        const std::array xs{
            layout.first.left, layout.first.right, layout.second.left, layout.second.right};
        const std::array ys{
            layout.first.top, layout.first.bottom, layout.second.top, layout.second.bottom};
        auto& path = result.closed[i];
        for (const auto vertex : layout.vertices) {
            path.emplace_back(xs[static_cast<std::size_t>(vertex.x)],
                              ys[static_cast<std::size_t>(vertex.y)]);
        }
    }
    result.open.clear();
    transaction.finish();
}

auto execute_two_rectangle_intersection(const Rect64&, const Rect64&, paths64_result&) -> void;
[[nodiscard]] auto try_execute_two_rectangle_union(const Rect64&,
                                                   const Rect64&,
                                                   paths64_result&) -> bool;
[[nodiscard]] auto try_execute_two_rectangle_difference(const Rect64&,
                                                        const Rect64&,
                                                        paths64_result&) -> bool;
[[nodiscard]] auto try_execute_two_rectangle_xor(const Rect64&,
                                                 const Rect64&,
                                                 paths64_result&) -> bool;

}  // namespace clipper2next::internal
