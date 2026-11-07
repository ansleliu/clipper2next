#pragma once

#include "clipper2next/core.h"
#include "geometry/private/geometry_predicates.h"
#include "support/private/storage/release_vector.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <limits>
#include <vector>

namespace clipper2next::internal {

[[nodiscard]] inline auto triangulation_bounds(Point64 a, Point64 b) noexcept -> Rect64 {
    return {std::min(a.x, b.x), std::min(a.y, b.y),
            std::max(a.x, b.x), std::max(a.y, b.y)};
}

[[nodiscard]] inline auto triangulation_bounds_overlap(const Rect64& a,
                                                       const Rect64& b) noexcept -> bool {
    return a.left <= b.right && b.left <= a.right &&
           a.top <= b.bottom && b.top <= a.bottom;
}

// One admitted query owns its coefficients and exact arithmetic-domain proof.
class triangulation_segment_probe final {
    Point64 a_, b_;
    Rect64 query_bounds_;
    int64_t dx_, dy_;
    bool narrow_;
    static auto magnitude(int64_t value) noexcept -> uint64_t {
        return value < 0 ? uint64_t{0} - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    }
    auto side(Point64 point) const -> int {
        if (!narrow_) { return cross_product_sign_in_clipper_range(a_, b_, point); }
        const auto determinant = dx_ * (point.y - a_.y) - dy_ * (point.x - a_.x);
        return (determinant > 0) - (determinant < 0);
    }
public:
    triangulation_segment_probe(Point64 a, Point64 b, const Rect64& root)
        : a_(a), b_(b), query_bounds_(triangulation_bounds(a, b)), dx_(b.x-a.x), dy_(b.y-a.y),
          narrow_((magnitude(dx_) | magnitude(dy_) | magnitude(root.left-a.x) | magnitude(root.right-a.x) |
                   magnitude(root.top-a.y) | magnitude(root.bottom-a.y)) < (uint64_t{1} << 31)) {}
    [[nodiscard]] auto operator()(const Rect64& bounds) const -> bool {
        if (!triangulation_bounds_overlap(query_bounds_, bounds)) { return false; }
        const Point64 low{dy_ >= 0 ? bounds.right : bounds.left,
                          dx_ >= 0 ? bounds.top : bounds.bottom};
        const Point64 high{dy_ >= 0 ? bounds.left : bounds.right,
                           dx_ >= 0 ? bounds.bottom : bounds.top};
        return side(low) <= 0 && side(high) >= 0;
    }
};

// All coordinates and doubled midpoint components remain inside signed64
// under the existing public coordinate admission. Equality keeps the exact
// infinitesimal direction instead of rounding a midpoint onto the boundary.
[[nodiscard]] inline auto triangulation_probe_in_bounds(const Rect64& bounds,
                                                        Point64 a, Point64 b,
                                                        int64_t dx, int64_t dy) noexcept -> bool {
    const auto x = a.x + b.x, y = a.y + b.y;
    const auto left = bounds.left + bounds.left, right = bounds.right + bounds.right;
    const auto top = bounds.top + bounds.top, bottom = bounds.bottom + bounds.bottom;
    return !(x < left || x > right || y < top || y > bottom ||
             (x == left && dx < 0) || (x == right && dx > 0) ||
             (y == top && dy < 0) || (y == bottom && dy > 0));
}

// A static directory of admitted closed boxes. Original ordinals never follow
// the partition order, so graph view sorting cannot change payload identity.
class triangulation_bounds_catalog final {
    static constexpr std::size_t leaf_capacity = 4U;
    struct node {
        Rect64 bounds;
        std::size_t first;
        std::size_t count;
        std::size_t left = 0;
        std::size_t right = 0;
    };
    std::vector<Rect64> bounds_;
    std::vector<std::size_t> order_;
    std::vector<node> nodes_;

    static auto spread_coordinate(uint64_t value) noexcept -> uint64_t {
        // The directory has two axes. Removing the unused third-axis bits
        // preserves the exact stable Morton order and shortens the radix key.
        value = (value | (value << 16)) & UINT64_C(0x0000ffff0000ffff);
        value = (value | (value << 8)) & UINT64_C(0x00ff00ff00ff00ff);
        value = (value | (value << 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
        value = (value | (value << 2)) & UINT64_C(0x3333333333333333);
        return (value | (value << 1)) & UINT64_C(0x5555555555555555);
    }

    auto order_records() -> void {
        auto root = bounds_.front();
        for (const auto& bounds : bounds_) {
            root.left = std::min(root.left, bounds.left);
            root.right = std::max(root.right, bounds.right);
            root.top = std::min(root.top, bounds.top);
            root.bottom = std::max(root.bottom, bounds.bottom);
        }
        // Admitted doubled extents and relative centres are below 2^63.
        // Quantized keys select layout only; exact closed boxes own all queries.
        const auto xbits = std::bit_width(static_cast<uint64_t>(root.right - root.left) * 2U);
        const auto ybits = std::bit_width(static_cast<uint64_t>(root.bottom - root.top) * 2U);
        const auto xshift = xbits > 21 ? xbits - 21 : 0;
        const auto yshift = ybits > 21 ? ybits - 21 : 0;
        struct key_record { uint64_t key; std::size_t ordinal; };
        std::vector<key_record> keys(bounds_.size()), temporary(bounds_.size());
        uint64_t key_bits = 0;
        for (std::size_t i = 0; i != bounds_.size(); ++i) {
            const auto& bounds = bounds_[i];
            const auto x = static_cast<uint64_t>((bounds.left - root.left) +
                                                (bounds.right - root.left)) >> xshift;
            const auto y = static_cast<uint64_t>((bounds.top - root.top) +
                                                (bounds.bottom - root.top)) >> yshift;
            keys[i] = {spread_coordinate(x) | (spread_coordinate(y) << 1), i};
            key_bits |= keys[i].key;
        }
        // Stable passes preserve distinct ordinal identity when keys coincide.
        // All higher bytes are zero for every key and cannot change that order.
        const auto significant_bits = static_cast<unsigned>(std::bit_width(key_bits));
        for (unsigned shift = 0; shift < significant_bits; shift += 8) {
            std::array<std::size_t, 256> positions{};
            for (const auto& item : keys) { ++positions[(item.key >> shift) & 255U]; }
            std::size_t prefix = 0;
            for (auto& position : positions) {
                const auto count = position;
                position = prefix;
                prefix += count;
            }
            for (const auto& item : keys) {
                temporary[positions[(item.key >> shift) & 255U]++] = item;
            }
            keys.swap(temporary);
        }
        for (std::size_t i = 0; i != keys.size(); ++i) { order_[i] = keys[i].ordinal; }
    }

    auto make_node(std::size_t first, std::size_t count) -> std::size_t {
        const auto id = nodes_.size();
        nodes_.push_back({bounds_[order_[first]], first, count});
        if (count <= leaf_capacity) {
            auto& bounds = nodes_[id].bounds;
            for (std::size_t i = first + 1; i != first + count; ++i) {
                const auto& item = bounds_[order_[i]];
                bounds.left = std::min(bounds.left, item.left);
                bounds.right = std::max(bounds.right, item.right);
                bounds.top = std::min(bounds.top, item.top);
                bounds.bottom = std::max(bounds.bottom, item.bottom);
            }
            return id;
        }
        const auto middle = first + count / 2;
        const auto left = make_node(first, middle - first);
        const auto right = make_node(middle, first + count - middle);
        const auto& a = nodes_[left].bounds;
        const auto& b = nodes_[right].bounds;
        nodes_[id].bounds = {std::min(a.left, b.left), std::min(a.top, b.top),
                             std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
        nodes_[id].left = left;
        nodes_[id].right = right;
        return id;
    }

    template <typename Intersects, typename Visitor>
    [[nodiscard]] auto query_node(std::size_t id, Intersects& intersects,
                                  Visitor& visit) const -> bool {
        const auto& item = nodes_[id];
        if (!intersects(item.bounds)) { return true; }
        if (item.left) {
            return query_node(item.left, intersects, visit) && query_node(item.right, intersects, visit);
        }
        for (auto i = item.first; i != item.first + item.count; ++i) {
            const auto ordinal = order_[i];
            if (intersects(bounds_[ordinal]) && !visit(ordinal)) { return false; }
        }
        return true;
    }

public:
    template <typename Bounds>
    auto build(std::size_t count, Bounds&& get_bounds) -> void {
        clear();
        bounds_.resize(count);
        order_.resize(count);
        // With leaves of at most four and midpoint splits, node count <= count.
        // Avoid a speculative 2*count reserve or an overflowing size expression.
        nodes_.reserve(count);
        for (std::size_t i = 0; i != count; ++i) {
            bounds_[i] = get_bounds(i);
            order_[i] = i;
        }
        if (count > leaf_capacity) { order_records(); }
        if (count) { (void)make_node(0, count); }
    }

    [[nodiscard]] auto bounds(std::size_t ordinal) const noexcept -> const Rect64& {
        return bounds_[ordinal];
    }

    template <typename Visitor>
    [[nodiscard]] auto query(const Rect64& bounds, Visitor&& visit) const -> bool {
        const auto intersects = [&](const Rect64& item) { return triangulation_bounds_overlap(item, bounds); };
        return nodes_.empty() || query_node(0, intersects, visit);
    }


    template <typename Visitor>
    [[nodiscard]] auto query_segment(Point64 a, Point64 b, Visitor&& visit) const -> bool {
        if (nodes_.empty()) { return true; }
        const auto intersects = triangulation_segment_probe{a, b, nodes_.front().bounds};
        return query_node(0, intersects, visit);
    }

    // Each overlapping unordered pair of original records is visited once.
    // Pair ordinals are ascending; traversal order is not part of the contract.
    template <typename Visitor>
    [[nodiscard]] auto query_pairs(Visitor&& visit) const -> bool {
        if (nodes_.empty()) { return true; }
        struct node_pair { std::size_t first, second; };
        // Midpoint splits bound each node depth by size_t digits. A binary
        // fork adds one pending pair per depth; a self fork adds two for two
        // depths. The same depth-first order therefore needs at most 2D+1 slots.
        std::array<node_pair, 2U * std::numeric_limits<std::size_t>::digits + 1U> pending;
        std::size_t count = 0;
        const auto push = [&](std::size_t first, std::size_t second) {
            assert(count < pending.size());
            pending[count++] = {first, second};
        };
        push(0, 0);
        while (count) {
            const auto pair = pending[--count];
            const auto a_id = pair.first, b_id = pair.second;
            const auto& a = nodes_[a_id];
            const auto& b = nodes_[b_id];
            if (!triangulation_bounds_overlap(a.bounds, b.bounds)) { continue; }
            if (a_id == b_id && a.left) {
                push(a.right, a.right);
                push(a.left, a.right);
                push(a.left, a.left);
            } else if (!a.left && !b.left) {
                if (a_id == b_id) {
                    for (auto i = a.first; i < a.first + a.count; ++i) {
                        for (auto j = i + 1U; j < a.first + a.count; ++j) {
                            const auto first = order_[i], second = order_[j];
                            if (triangulation_bounds_overlap(bounds_[first], bounds_[second]) &&
                                !visit(std::min(first, second), std::max(first, second))) { return false; }
                        }
                    }
                } else {
                    // A record outside the other leaf's union cannot contribute
                    // a pair. Keep original record order for callbacks/early stop.
                    std::array<std::size_t, leaf_capacity> candidates;
                    std::size_t candidate_count = 0;
                    for (auto j = b.first; j < b.first + b.count; ++j) {
                        const auto second = order_[j];
                        if (triangulation_bounds_overlap(a.bounds, bounds_[second])) {
                            candidates[candidate_count++] = second;
                        }
                    }
                    if (candidate_count == 0) { continue; }
                    for (auto i = a.first; i < a.first + a.count; ++i) {
                        const auto first = order_[i];
                        if (!triangulation_bounds_overlap(bounds_[first], b.bounds)) { continue; }
                        for (std::size_t j = 0; j < candidate_count; ++j) {
                            const auto second = candidates[j];
                            if (triangulation_bounds_overlap(bounds_[first], bounds_[second]) &&
                                !visit(std::min(first, second), std::max(first, second))) { return false; }
                        }
                    }
                }
            } else if (a.left && (!b.left || a.count >= b.count)) {
                push(a.right, b_id);
                push(a.left, b_id);
            } else {
                push(a_id, b.right);
                push(a_id, b.left);
            }
        }
        return true;
    }

    auto clear() noexcept -> void {
        nodes_.clear();
        order_.clear();
        bounds_.clear();
    }

    auto release() noexcept -> void {
        clear();
        release_vector(nodes_);
        release_vector(order_);
        release_vector(bounds_);
    }
};

}  // namespace clipper2next::internal
