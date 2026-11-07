#pragma once

#include "rectclip/private/rectclip_graph.h"

#include <cstddef>

namespace clipper2next::internal {
auto prepare_polygon_path(rectclip_node*& node) -> void;

inline auto append_materialized_rectclip_path(
    const rectclip_node* start, Paths64& output) -> void {
    auto count = std::size_t{1U};
    for (auto* node = start->next.get(); node != start; node = node->next.get()) { ++count; }
    auto& result = output.emplace_back();
    result.reserve(count);
    auto* node = start;
    do {
        result.emplace_back(node->pt);
        node = node->next.get();
    } while (node != start);
}

}  // namespace clipper2next::internal
