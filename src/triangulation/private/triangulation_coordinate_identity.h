#pragma once

#include "triangulation/private/triangulation_context.h"
#include "support/private/checked_size.h"
#include <bit>
#include <limits>
#include <vector>

namespace clipper2next::internal {

// Normalized splits reuse source endpoints, so unique coordinates cannot exceed
// source segment count. Buckets are lookup-only; vertex creation keeps call order.
class triangulation_coordinate_identity final {
    // The graph owns immutable coordinates; buckets only identify their vertices.
    std::vector<triangulation_vertex_ref> entries_;

    static auto capacity(std::size_t count) -> std::size_t {
        const auto needed = checked_size_multiply(std::max(count, std::size_t{1}), 2U);
        const auto highest = std::size_t{1}
            << (std::numeric_limits<std::size_t>::digits - 1);
        if (needed > highest) { throw std::length_error{"clipper2next capacity overflow"}; }
        return std::bit_ceil(needed);
    }
    static auto mix(std::uint64_t value) noexcept -> std::uint64_t {
        value ^= value >> 30;
        value *= std::uint64_t{0xbf58476d1ce4e5b9};
        value ^= value >> 27;
        value *= std::uint64_t{0x94d049bb133111eb};
        return value ^ (value >> 31);
    }
    static auto hash(Point64 point) noexcept -> std::size_t {
        return static_cast<std::size_t>(
            mix(static_cast<std::uint64_t>(point.x)) ^
            std::rotl(mix(static_cast<std::uint64_t>(point.y)), 1));
    }

public:
    explicit triangulation_coordinate_identity(std::size_t source_segments)
        : entries_(capacity(source_segments)) {}

    auto vertex(triangulation_context& context, Point64 point) -> triangulation_vertex* {
        const auto mask = entries_.size() - 1U;
        auto ordinal = hash(point) & mask;
        while (entries_[ordinal] && entries_[ordinal]->pt != point) {
            ordinal = (ordinal + 1U) & mask;
        }
        auto& found = entries_[ordinal];
        if (!found) { found = create_triangulation_vertex(context, point); }
        return found.get();
    }
};

}  // namespace clipper2next::internal
