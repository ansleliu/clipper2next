#pragma once

#include <cstddef>
#include <cstdint>

#include "rectclip/private/rectclip_graph.h"

namespace clipper2next::internal {

auto add_to_edge(rectclip_node_list& edge, rectclip_node* node) -> void;

auto uncouple_edge(rectclip_node* node) -> void;

auto set_new_owner(rectclip_node* node, std::size_t new_index) -> void;

[[nodiscard]] auto get_edges_for_point(const Point64& point, const Rect64& rect) -> std::uint32_t;

auto check_edges(rectclip_node_list& results, rectclip_node_list* edges, const Rect64& rect)
    -> void;

auto tidy_edges(std::size_t index,
                rectclip_node_list& clockwise,
                rectclip_node_list& counter_clockwise,
                rectclip_node_list& results) -> void;

}  // namespace clipper2next::internal
