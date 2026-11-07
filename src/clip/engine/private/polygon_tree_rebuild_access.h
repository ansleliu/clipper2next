#pragma once

#include "clipper2next/polygon/tree.h"
#include <algorithm>
#include <array>

namespace clipper2next {

struct polygon_tree_rebuild_access final {
    template <typename C>
    static auto size(const polygon_tree<C>& tree) noexcept -> std::size_t {
        return tree.nodes_.size();
    }
    template <typename C, typename Layout>
    static auto prepare(polygon_tree<C>& tree, const Layout& layout) -> void {
        const auto original = tree.nodes_.size();
        try {
            tree.nodes_.resize((std::max)(original, layout.size()));
            for (std::size_t i = 0; i < layout.size(); ++i) {
                tree.nodes_[i].polygon.reserve(layout[i].point_count);
                tree.nodes_[i].children.reserve(layout[i].child_count);
            }
        } catch (...) {
            tree.nodes_.resize(original);
            throw;
        }
    }
    template <typename C>
    static auto resize(polygon_tree<C>& tree, std::size_t size) noexcept -> void {
        tree.nodes_.resize(size);
    }
    template <typename C>
    static auto begin(polygon_tree<C>& tree) noexcept -> void {
        for (auto& node : tree.nodes_) {
            node.polygon.clear();
            node.children.clear();
            node.parent = {};
            node.depth = 0;
        }
    }
    template <typename C>
    static auto cancel(polygon_tree<C>& tree) noexcept -> void {
        begin(tree);
        // An implicitly moved-from tree may have no root. Cancellation preserves
        // that empty representation; only fallible preparation creates nodes.
        tree.nodes_.resize((std::min)(std::size_t{1}, tree.nodes_.size()));
    }
    template <typename C>
    static auto materialize_empty(polygon_tree<C>& tree) -> void {
        struct root_layout {
            std::size_t point_count;
            std::size_t child_count;
        };
        prepare(tree, std::array<root_layout, 1>{{{0U, 0U}}});
        cancel(tree);
    }
    template <typename C>
    static auto polygon(polygon_tree<C>& tree, std::size_t index) noexcept -> Path<C>& {
        return tree.nodes_[index].polygon;
    }
    template <typename C>
    static auto link(polygon_tree<C>& tree, std::size_t index, std::size_t parent, unsigned depth)
        -> void {
        auto& node = tree.nodes_[index];
        node.parent = {parent};
        node.depth = depth;
        tree.nodes_[parent].children.push_back({index});
    }
};

}  // namespace clipper2next
