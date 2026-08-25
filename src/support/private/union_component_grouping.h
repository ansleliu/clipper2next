#pragma once

#include "clipper2next/core/rect.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

namespace clipper2next::internal {

[[nodiscard]] inline auto find_union_component_root(std::vector<std::size_t>& parents,
                                                    std::size_t index) -> std::size_t {
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

inline auto merge_union_components(std::vector<std::size_t>& parents,
                                   std::size_t first,
                                   std::size_t second) -> void {
    first = find_union_component_root(parents, first);
    second = find_union_component_root(parents, second);
    if (first == second) { return; }
    if (second < first) { std::swap(first, second); }
    parents[second] = first;
}

[[nodiscard]] inline auto group_union_components_by_bounds(
    const std::vector<Rect64>& path_bounds) -> std::vector<std::vector<std::size_t>> {
    std::vector<std::size_t> parents(path_bounds.size());
    std::iota(parents.begin(), parents.end(), std::size_t{0});

    std::vector<std::size_t> ordered_indices;
    ordered_indices.reserve(path_bounds.size());
    for (std::size_t index = 0; index < path_bounds.size(); ++index) {
        if (path_bounds[index].is_valid()) { ordered_indices.push_back(index); }
    }

    std::sort(ordered_indices.begin(),
              ordered_indices.end(),
              [&path_bounds](std::size_t first, std::size_t second) {
                  const auto& first_bounds = path_bounds[first];
                  const auto& second_bounds = path_bounds[second];
                  if (first_bounds.left != second_bounds.left) {
                      return first_bounds.left < second_bounds.left;
                  }
                  if (first_bounds.right != second_bounds.right) {
                      return first_bounds.right < second_bounds.right;
                  }
                  return first < second;
              });

    std::vector<std::size_t> active_indices;
    active_indices.reserve(ordered_indices.size());
    for (const auto current_index : ordered_indices) {
        const auto& current_bounds = path_bounds[current_index];
        std::erase_if(active_indices, [&path_bounds, &current_bounds](std::size_t index) {
            return path_bounds[index].right < current_bounds.left;
        });

        for (const auto active_index : active_indices) {
            if (path_bounds[active_index].intersects(current_bounds)) {
                merge_union_components(parents, active_index, current_index);
            }
        }
        active_indices.push_back(current_index);
    }

    std::vector<std::vector<std::size_t>> components(path_bounds.size());
    for (std::size_t index = 0; index < path_bounds.size(); ++index) {
        if (path_bounds[index].is_valid()) {
            components[find_union_component_root(parents, index)].push_back(index);
        }
    }
    return components;
}

}  // namespace clipper2next::internal
