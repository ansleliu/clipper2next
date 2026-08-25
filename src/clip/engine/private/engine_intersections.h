#pragma once

#include "clip/engine/private/engine_types.h"
#include "clipper2next/geometry/line_intersections.h"

#include <vector>

namespace clipper2next::internal {

auto add_intersection_node(IntersectNodeList& intersections,
                           active_edge_node& first,
                           active_edge_node& second,
                           int64_t top_y,
                           int64_t bottom_y,
                           predicate_policy policy = {}) -> void;

auto build_intersection_list_from_sel(active_edge_node*& sorted_edges,
                                      IntersectNodeList& intersections,
                                      int64_t top_y,
                                      int64_t bottom_y,
                                      predicate_policy policy = {}) -> bool;

auto build_intersection_list_from_sel(active_edge_node_ref& sorted_edges,
                                      IntersectNodeList& intersections,
                                      int64_t top_y,
                                      int64_t bottom_y,
                                      predicate_policy policy = {}) -> bool;

auto build_intersection_list_from_contiguous_unit_runs(
    std::vector<active_edge_node*>& edges,
    std::vector<active_edge_node*>& scratch,
    IntersectNodeList& intersections,
    int64_t top_y,
    int64_t bottom_y,
    predicate_policy policy = {}) -> bool;

[[nodiscard]] auto compare_intersections_bottom_up(const IntersectNode& first,
                                                   const IntersectNode& second) -> bool;

[[nodiscard]] auto intersection_edges_are_adjacent_in_ael(const IntersectNode& node) noexcept
    -> bool;

[[nodiscard]] auto find_next_adjacent_intersection(IntersectNodeList::iterator first,
                                                   IntersectNodeList::iterator last)
    -> IntersectNodeList::iterator;

auto sort_intersections(IntersectNodeList& intersections) -> void;

}  // namespace clipper2next::internal
