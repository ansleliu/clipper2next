#pragma once

#include "clipper2next/geotypes/topology.hpp"

#include <algorithm>
#include <expected>
#include <limits>

namespace geotypes {

enum class StructureError {
    InvalidClosure,
    InvalidRole,
    PointRange,
    RingRange,
    ParentRange,
    ParentCycle,
    ScratchTooSmall,
};

// Structural checks require live spans. Their result remains applicable only
// while the underlying storage and descriptors remain unchanged. They do not
// validate geometry, extend storage lifetime, or allocate memory.
template <typename T>
[[nodiscard]] constexpr auto validateStructure(PathView<T> view)
    -> std::expected<void, StructureError> {
    if (view.closure != PathClosure::Open && view.closure != PathClosure::ClosedImplicit &&
        view.closure != PathClosure::ClosedRepeated) {
        return std::unexpected(StructureError::InvalidClosure);
    }
    return {};
}

namespace detail {

[[nodiscard]] constexpr auto validDescriptorRange(
    std::uint32_t offset, std::uint32_t count, std::size_t size) noexcept -> bool {
    return offset <= size && count <= size - offset;
}

}  // namespace detail

template <typename T>
[[nodiscard]] constexpr auto validateStructure(PathSetView<T> view)
    -> std::expected<void, StructureError> {
    for (const auto& path : view.paths) {
        if (!detail::validDescriptorRange(path.pointOffset, path.pointCount, view.points.size())) {
            return std::unexpected(StructureError::PointRange);
        }
        const auto closure = validateStructure(PathView<T>{{}, path.closure});
        if (!closure) { return closure; }
    }
    return {};
}

template <typename T>
[[nodiscard]] constexpr auto validateStructure(RingSetView<T> view)
    -> std::expected<void, StructureError> {
    for (const auto& ring : view.rings) {
        if (ring.role != RingRole::Shell && ring.role != RingRole::Hole) {
            return std::unexpected(StructureError::InvalidRole);
        }
        if (!detail::validDescriptorRange(ring.pointOffset, ring.pointCount, view.points.size())) {
            return std::unexpected(StructureError::PointRange);
        }
    }
    return {};
}

// Requires one scratch byte per polygon, disjoint from the view's storage;
// trailing bytes are untouched. Parent
// traversal is iterative O(P), with each node visited/finalized at most once.
// A pure ring consumer should validate ringSet(), not unused polygon parents.
template <typename T>
[[nodiscard]] constexpr auto validateStructure(
    TopologyView<T> view, std::span<std::uint8_t> scratch)
    -> std::expected<void, StructureError> {
    const auto rings = validateStructure(view.ringSet());
    if (!rings) { return rings; }
    for (const auto& polygon : view.polygons) {
        if (!detail::validDescriptorRange(polygon.ringOffset, polygon.ringCount, view.rings.size())) {
            return std::unexpected(StructureError::RingRange);
        }
        if (polygon.parentPolygon != noPolygonIndex && polygon.parentPolygon >= view.polygons.size()) {
            return std::unexpected(StructureError::ParentRange);
        }
    }
    if (scratch.size() < view.polygons.size()) {
        return std::unexpected(StructureError::ScratchTooSmall);
    }
    auto state = scratch.first(view.polygons.size());
    std::ranges::fill(state, std::uint8_t{});
    constexpr auto none = (std::numeric_limits<std::size_t>::max)();
    const auto parent = [&](std::size_t index) {
        const auto value = view.polygons[index].parentPolygon;
        return value == noPolygonIndex ? none : static_cast<std::size_t>(value);
    };
    for (std::size_t root = 0; root < view.polygons.size(); ++root) {
        auto current = root;
        while (current != none && state[current] == 0) {
            state[current] = 1;
            current = parent(current);
        }
        if (current != none && state[current] == 1) {
            return std::unexpected(StructureError::ParentCycle);
        }
        current = root;
        while (current != none && state[current] == 1) {
            state[current] = 2;
            current = parent(current);
        }
    }
    return {};
}

}  // namespace geotypes
