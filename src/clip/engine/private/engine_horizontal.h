#pragma once

#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_geometry.h"
#include "clip/engine/private/engine_topology.h"
#include "clip/engine/private/engine_types.h"

namespace clipper2next::internal {

class engine_output_owner;

auto trim_horizontal(active_edge_node& horizontal_edge, bool preserve_collinear) noexcept -> void;

auto push_horizontal(active_edge_node*& horizontal_stack, active_edge_node& edge) noexcept -> void;
auto push_horizontal(active_edge_node_ref& horizontal_stack, active_edge_node& edge) noexcept
    -> void;
[[nodiscard]] auto pop_horizontal(active_edge_node*& horizontal_stack,
                                  active_edge_node*& edge) noexcept -> bool;
[[nodiscard]] auto pop_horizontal(active_edge_node_ref& horizontal_stack,
                                  active_edge_node*& edge) noexcept -> bool;

[[nodiscard]] auto reset_horizontal_direction(const active_edge_node& horizontal_edge,
                                              const Vertex* max_vertex,
                                              int64_t& horizontal_left,
                                              int64_t& horizontal_right) noexcept -> bool;

auto add_trial_horizontal_join(HorzSegmentList& horizontal_segments,
                               output_point_node* output_point) -> void;

auto convert_horizontal_segments_to_joins(HorzSegmentList& horizontal_segments,
                                          std::vector<horizontal_join_node>& horizontal_joins)
    -> void;

auto process_horizontal_joins(std::vector<horizontal_join_node>& horizontal_joins,
                              engine_output_owner& output_owner,
                              bool using_polytree) -> void;

[[nodiscard]] auto compare_horizontal_segments(const horizontal_segment_node& first,
                                               const horizontal_segment_node& second) -> bool;

[[nodiscard]] auto set_horizontal_segment_heading_forward(horizontal_segment_node& segment,
                                                          output_point_node& previous,
                                                          output_point_node& next) noexcept -> bool;

[[nodiscard]] auto update_horizontal_segment(horizontal_segment_node& segment) noexcept -> bool;

[[nodiscard]] auto get_extended_horizontal_segment(output_point_node*& first,
                                                   output_point_node*& second) noexcept -> bool;

auto sort_horizontal_segments(HorzSegmentList& segments) -> void;

}  // namespace clipper2next::internal
