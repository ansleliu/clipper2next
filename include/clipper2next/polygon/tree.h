#pragma once

#include "clipper2next/geometry.h"
#include "clipper2next/geometry/scale.h"

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace clipper2next {

template <typename Coordinate>
class polygon_tree {
public:
    using coordinate_type = Coordinate;
    using path_type = Path<Coordinate>;

    struct node_id {
        std::size_t value = 0;

        friend auto operator==(node_id lhs, node_id rhs) noexcept -> bool {
            return lhs.value == rhs.value;
        }

        friend auto operator!=(node_id lhs, node_id rhs) noexcept -> bool { return !(lhs == rhs); }
    };

    polygon_tree() { reset_root(); }

    [[nodiscard]] auto root() const noexcept -> node_id { return {}; }

    auto clear() -> void {
        nodes_.clear();
        reset_root();
    }

    auto reserve(std::size_t node_capacity) -> void { nodes_.reserve(node_capacity); }

    auto reserve_children(node_id id, std::size_t child_capacity) -> void {
        nodes_.at(id.value).children.reserve(child_capacity);
    }

    template <typename Source>
    [[nodiscard]] auto add_child(node_id parent, const Path<Source>& path) -> node_id {
        const auto depth = nodes_.at(parent.value).depth + 1U;
        auto polygon = make_path(path);
        const auto id = node_id{nodes_.size()};
        // emplace_back may reallocate, so link the child through a fresh
        // lookup instead of a reference taken before the insertion.
        nodes_.emplace_back(parent, std::move(polygon), depth);
        nodes_[parent.value].children.push_back(id);
        return id;
    }

    [[nodiscard]] auto add_child(node_id parent, path_type&& path) -> node_id {
        const auto depth = nodes_.at(parent.value).depth + 1U;
        path_type polygon{std::move(path)};
        const auto id = node_id{nodes_.size()};
        // emplace_back may reallocate, so link the child through a fresh
        // lookup instead of a reference taken before the insertion.
        nodes_.emplace_back(parent, std::move(polygon), depth);
        nodes_[parent.value].children.push_back(id);
        return id;
    }

    template <typename Source>
    [[nodiscard]] auto add_child(const Path<Source>& path) -> node_id {
        return add_child(root(), path);
    }

    [[nodiscard]] auto children(node_id id) const -> std::span<const node_id> {
        const auto& child_nodes = nodes_.at(id.value).children;
        return std::span<const node_id>{child_nodes.data(), child_nodes.size()};
    }

    [[nodiscard]] auto child(node_id parent, std::size_t index) const -> node_id {
        return nodes_.at(parent.value).children.at(index);
    }

    [[nodiscard]] auto parent(node_id id) const -> node_id { return nodes_.at(id.value).parent; }

    [[nodiscard]] auto polygon(node_id id) const -> const path_type& {
        return nodes_.at(id.value).polygon;
    }

    [[nodiscard]] auto depth(node_id id) const -> unsigned { return nodes_.at(id.value).depth; }

    [[nodiscard]] auto is_hole(node_id id) const -> bool {
        const auto value = depth(id);
        return value != 0U && (value % 2U) == 0U;
    }

    [[nodiscard]] auto count(node_id id) const -> std::size_t {
        return nodes_.at(id.value).children.size();
    }

    [[nodiscard]] auto count() const -> std::size_t { return count(root()); }

    [[nodiscard]] auto area(node_id id) const -> double {
        // Iterative traversal: recursion would overflow the stack on
        // pathologically deep nesting.
        double result = 0.0;
        std::vector<node_id> pending{id};
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (current.value != root().value) {
                result += clipper2next::area(nodes_.at(current.value).polygon);
            }
            const auto& children = nodes_.at(current.value).children;
            pending.insert(pending.end(), children.begin(), children.end());
        }
        return result;
    }

    [[nodiscard]] auto area() const -> double { return area(root()); }

    auto set_scale(double value) noexcept -> void { scale_ = value; }

    [[nodiscard]] auto scale() const noexcept -> double { return scale_; }

private:
    struct node {
        node_id parent{};
        path_type polygon;
        std::vector<node_id> children;
        unsigned depth = 0;

        node() = default;

        node(node_id parent_value,
             path_type polygon_value,
             unsigned depth_value)
            : parent(parent_value),
              polygon(std::move(polygon_value)),
              depth(depth_value) {}
    };

    auto reset_root() -> void { nodes_.emplace_back(); }

    template <typename Source>
    [[nodiscard]] auto make_path(const Path<Source>& path) const -> path_type {
        if constexpr (std::is_same_v<Coordinate, double> && std::is_integral_v<Source>) {
            auto scaled_path = scale_path<double, Source>(path, scale_);
            if (!scaled_path.has_value()) { raise_clipper_error(scaled_path.error()); }
            path_type result;
            result.reserve(scaled_path.value().size());
            for (const auto& point : scaled_path.value()) { result.emplace_back(point); }
            return result;
        } else {
            path_type result;
            result.reserve(path.size());
            for (const auto& point : path) { result.emplace_back(point); }
            return result;
        }
    }

    std::vector<node> nodes_;
    double scale_ = 1.0;
};

using polygon_tree64 = polygon_tree<int64_t>;
using polygon_tree_d = polygon_tree<double>;

}  // namespace clipper2next
