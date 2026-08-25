#pragma once

#include "clip/engine/private/engine_scanbeam_orchestrator.h"

namespace clipper2next::internal {

[[nodiscard]] auto is_joined(const active_edge_node& edge) noexcept -> bool;
[[nodiscard]] auto current_y_maxima_vertex_open(const active_edge_node& edge) -> Vertex*;
[[nodiscard]] auto current_y_maxima_vertex(const active_edge_node& edge) -> Vertex*;
[[nodiscard]] auto maxima_pair(const active_edge_node& edge) noexcept -> active_edge_node*;
[[nodiscard]] auto last_output_point(const active_edge_node& hot_edge) noexcept
    -> output_point_node*;

auto update_scanbeam_edge(clipper_base_state& state,
                          active_edge_node& edge,
                          bool preserve_collinear,
                          bool& succeeded) -> void;

auto add_local_max_poly(clipper_base_state& state,
                        active_edge_node& first,
                        active_edge_node& second,
                        const Point64& point,
                        bool& succeeded) -> output_point_node*;

}  // namespace clipper2next::internal
