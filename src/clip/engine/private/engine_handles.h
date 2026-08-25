// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <cstdint>

namespace clipper2next::internal {

struct edge_id {
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return generation != 0; }

    [[nodiscard]] friend constexpr auto operator==(edge_id, edge_id) noexcept -> bool = default;
};

struct vertex_id {
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return generation != 0; }

    [[nodiscard]] friend constexpr auto operator==(vertex_id, vertex_id) noexcept -> bool = default;
};

struct output_point_id {
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return generation != 0; }

    [[nodiscard]] friend constexpr auto operator==(output_point_id, output_point_id) noexcept
        -> bool = default;
};

struct output_record_id {
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return generation != 0; }

    [[nodiscard]] friend constexpr auto operator==(output_record_id, output_record_id) noexcept
        -> bool = default;
};

}  // namespace clipper2next::internal
