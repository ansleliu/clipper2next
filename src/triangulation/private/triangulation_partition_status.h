#pragma once

#include "triangulation/private/triangulation_types.h"
#include "triangulation/private/triangulation_boundary_geometry.h"
#include "triangulation/private/triangulation_exact_predicates.h"
#include <algorithm>
#include <span>
#include <vector>

namespace clipper2next::internal {

// Only simultaneously active admitted source edges are compared. Their source
// endpoints remain fixed; point lookup is limited to the current sweep event.
// Dense slots preserve source identity through rotations and erase.
class triangulation_partition_status final {
    struct entry {
        std::size_t left = no_boundary_event;
        std::size_t right = no_boundary_event;
        std::size_t parent = no_boundary_event;
        std::size_t helper = no_boundary_event;
        int height = 0;
    };
    std::span<const triangulation_edge_ref> sources_;
    const triangulation_predicate_domain& domain_;
    std::vector<entry> entries_;
    std::size_t root_ = no_boundary_event;

    [[noreturn]] static auto invalid() -> void {
        raise_clipper_error(clipper_error_code::internal_error);
    }
    auto height(std::size_t id) const noexcept -> int {
        return id == no_boundary_event ? 0 : entries_[id].height;
    }
    auto update(std::size_t id) noexcept -> void {
        entries_[id].height = 1 + std::max(height(entries_[id].left), height(entries_[id].right));
    }
    auto balance(std::size_t id) const noexcept -> int {
        return height(entries_[id].left) - height(entries_[id].right);
    }
    auto replace(std::size_t old, std::size_t replacement) -> void {
        const auto parent = entries_[old].parent;
        if (parent == no_boundary_event) { root_ = replacement; }
        else if (entries_[parent].left == old) { entries_[parent].left = replacement; }
        else if (entries_[parent].right == old) { entries_[parent].right = replacement; }
        else { invalid(); }
        if (replacement != no_boundary_event) { entries_[replacement].parent = parent; }
    }
    auto rotate_left(std::size_t id) -> std::size_t {
        const auto pivot = entries_[id].right;
        if (pivot == no_boundary_event) { invalid(); }
        const auto middle = entries_[pivot].left;
        replace(id, pivot);
        entries_[pivot].left = id;
        entries_[id].parent = pivot;
        entries_[id].right = middle;
        if (middle != no_boundary_event) { entries_[middle].parent = id; }
        update(id);
        update(pivot);
        return pivot;
    }
    auto rotate_right(std::size_t id) -> std::size_t {
        const auto pivot = entries_[id].left;
        if (pivot == no_boundary_event) { invalid(); }
        const auto middle = entries_[pivot].right;
        replace(id, pivot);
        entries_[pivot].right = id;
        entries_[id].parent = pivot;
        entries_[id].left = middle;
        if (middle != no_boundary_event) { entries_[middle].parent = id; }
        update(id);
        update(pivot);
        return pivot;
    }
    auto rebalance(std::size_t id) -> void {
        while (id != no_boundary_event) {
            const auto previous_height = entries_[id].height;
            update(id);
            auto top = id;
            if (balance(id) > 1) {
                if (balance(entries_[id].left) < 0) { (void)rotate_left(entries_[id].left); }
                top = rotate_right(id);
            } else if (balance(id) < -1) {
                if (balance(entries_[id].right) > 0) { (void)rotate_right(entries_[id].right); }
                top = rotate_left(id);
            }
            if (entries_[top].height == previous_height) { break; }
            id = entries_[top].parent;
        }
    }
    auto before_edge(std::size_t first, std::size_t second) const -> bool {
        const auto& a = *sources_[first];
        const auto& b = *sources_[second];
        const auto sign = sweep_before(a.first->pt, b.first->pt)
            ? domain_.orientation(a.first->pt, a.second->pt, b.first->pt)
            : -domain_.orientation(b.first->pt, b.second->pt, a.first->pt);
        if (!sign) { invalid(); }
        return sign > 0;
    }

public:
    triangulation_partition_status(std::span<const triangulation_edge_ref> sources,
                                   const triangulation_predicate_domain& domain)
        : sources_(sources), domain_(domain), entries_(sources.size()) {}

    [[nodiscard]] auto empty() const noexcept -> bool { return root_ == no_boundary_event; }

    auto insert(std::size_t id, std::size_t helper) -> void {
        if (id >= entries_.size() || entries_[id].height) { invalid(); }
        auto parent = no_boundary_event;
        auto current = root_;
        bool left = false;
        while (current != no_boundary_event) {
            parent = current;
            left = before_edge(id, current);
            current = left ? entries_[current].left : entries_[current].right;
        }
        entries_[id] = {no_boundary_event, no_boundary_event, parent, helper, 1};
        if (parent == no_boundary_event) { root_ = id; }
        else if (left) { entries_[parent].left = id; }
        else { entries_[parent].right = id; }
        rebalance(parent);
    }

    auto erase(std::size_t id) -> void {
        if (id >= entries_.size() || !entries_[id].height) { invalid(); }
        auto start = entries_[id].parent;
        if (entries_[id].left == no_boundary_event) {
            replace(id, entries_[id].right);
        } else if (entries_[id].right == no_boundary_event) {
            replace(id, entries_[id].left);
        } else {
            auto successor = entries_[id].right;
            while (entries_[successor].left != no_boundary_event) {
                successor = entries_[successor].left;
            }
            if (entries_[successor].parent == id) {
                start = successor;
            } else {
                start = entries_[successor].parent;
                replace(successor, entries_[successor].right);
                entries_[successor].right = entries_[id].right;
                entries_[entries_[successor].right].parent = successor;
            }
            replace(id, successor);
            entries_[successor].left = entries_[id].left;
            entries_[entries_[successor].left].parent = successor;
            entries_[successor].height = entries_[id].height;
        }
        entries_[id] = {};
        rebalance(start);
    }

    [[nodiscard]] auto left(Point64 point) const -> std::size_t {
        auto result = no_boundary_event;
        auto current = root_;
        while (current != no_boundary_event) {
            const auto& edge = *sources_[current];
            const auto sign = domain_.orientation(edge.first->pt, edge.second->pt, point);
            if (!sign) { invalid(); }
            if (sign > 0) {
                result = current;
                current = entries_[current].right;
            } else {
                current = entries_[current].left;
            }
        }
        return result;
    }

    [[nodiscard]] auto helper(std::size_t id) const -> std::size_t {
        if (id >= entries_.size() || !entries_[id].height) { invalid(); }
        return entries_[id].helper;
    }
    auto set_helper(std::size_t id, std::size_t helper) -> void {
        if (id >= entries_.size() || !entries_[id].height) { invalid(); }
        entries_[id].helper = helper;
    }
};

}  // namespace clipper2next::internal
