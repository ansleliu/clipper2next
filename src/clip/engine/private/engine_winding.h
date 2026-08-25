#pragma once

#include "clip/engine/private/engine_fill_rule.h"
#include "clip/engine/private/engine_state.h"
#include "clip/engine/private/engine_types.h"

namespace clipper2next::internal {

inline auto is_hot_edge(const active_edge_node& edge) noexcept -> bool {
    return edge.outrec != nullptr;
}

inline auto is_open(const active_edge_node& edge) noexcept -> bool {
    return edge.local_min->is_open;
}

inline auto is_open_end(const Vertex& vertex) noexcept -> bool {
    return (vertex.flags & (VertexFlags::OpenStart | VertexFlags::OpenEnd)) != VertexFlags::Empty;
}

inline auto is_open_end(const active_edge_node& edge) noexcept -> bool {
    return is_open_end(*edge.vertex_top);
}

inline auto get_poly_type(const active_edge_node& edge) noexcept -> PathType {
    return edge.local_min->polytype;
}

inline auto is_same_poly_type(const active_edge_node& first,
                              const active_edge_node& second) noexcept -> bool {
    return first.local_min->polytype == second.local_min->polytype;
}

inline auto is_contributing_closed_edge(ClipType clip_type,
                                        FillRule fill_rule,
                                        const active_edge_node& edge) noexcept -> bool {
    return is_contributing_closed(
        clip_type, fill_rule, get_poly_type(edge), edge.winding_count, edge.wind_cnt2);
}

inline auto is_contributing_open_edge(ClipType clip_type,
                                      FillRule fill_rule,
                                      const active_edge_node& edge) noexcept -> bool {
    return is_contributing_open(clip_type, fill_rule, edge.winding_count, edge.wind_cnt2);
}

auto set_wind_count_for_closed_path_edge(clipper_base_state& state, active_edge_node& edge) -> void;

auto set_wind_count_for_open_path_edge(const clipper_base_state& state, active_edge_node& edge)
    -> void;

}  // namespace clipper2next::internal
