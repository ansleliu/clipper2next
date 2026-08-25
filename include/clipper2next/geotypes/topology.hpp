#pragma once

#include "clipper2next/geotypes/path.hpp"

#include <cstdint>
#include <limits>
#include <span>

namespace geotypes {

enum class RingRole : std::uint8_t {
    Shell,
    Hole,
};

inline constexpr auto noPolygonIndex = (std::numeric_limits<std::uint32_t>::max)();

struct RingDescriptor final {
    std::uint32_t pointOffset{};
    std::uint32_t pointCount{};
    RingRole role{RingRole::Shell};
    std::uint8_t reserved[3]{};
};

struct PolygonDescriptor final {
    std::uint32_t ringOffset{};
    std::uint32_t ringCount{};
    std::uint32_t parentPolygon{noPolygonIndex};
};

template <class T>
struct RingSetView final {
    std::span<const Point2<T>> points{};
    std::span<const RingDescriptor> rings{};

    [[nodiscard]] constexpr auto empty() const noexcept -> bool {
        return rings.empty();
    }
    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
        return rings.size();
    }
    [[nodiscard]] constexpr auto operator[](std::size_t index) const noexcept
        -> PathView<T> {
        const auto& ring = rings[index];
        return {
            points.subspan(ring.pointOffset, ring.pointCount),
            PathClosure::ClosedImplicit};
    }
};

template <class T>
struct TopologyView final {
    std::span<const Point2<T>> points{};
    std::span<const RingDescriptor> rings{};
    std::span<const PolygonDescriptor> polygons{};

    [[nodiscard]] constexpr auto ringSet() const noexcept
        -> RingSetView<T> {
        return {points, rings};
    }
};

using RingSetView64 = RingSetView<std::int64_t>;
using TopologyView64 = TopologyView<std::int64_t>;

}  // namespace geotypes
