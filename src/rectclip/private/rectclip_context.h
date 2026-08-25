#pragma once

#include "rectclip/private/rectclip_graph.h"
#include "support/private/storage/stable_pool.h"

#include <array>

namespace clipper2next::internal {

struct rectclip_context final {
    Rect64 rect;
    std::array<Point64, 4> rect_as_path;
    Point64 rect_midpoint;
    Rect64 path_bounds;
    stable_pool<rectclip_node> op_container;
    rectclip_node_list results;
    rectclip_node_list edges[8];
    std::vector<rect_location> start_locs;

    explicit rectclip_context(const Rect64& rect_) { reset_for_rect(rect_); }

    auto reset_for_rect(const Rect64& rect_) -> void;
    auto reset_polygon_storage() -> void;
    auto reset_line_storage() -> void;
    auto release() noexcept -> void;
};

}  // namespace clipper2next::internal
