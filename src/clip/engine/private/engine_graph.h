// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/core.h"
#include "clip/engine/private/engine_handles.h"

#include <vector>

namespace clipper2next::internal {

struct engine_edge_record {
    Point64 bottom{};
    Point64 top_point{};
};

class engine_graph final {
public:
    [[nodiscard]] auto create_edge(Point64 bottom, Point64 top_point) -> edge_id;
    [[nodiscard]] auto create_output_record() -> output_record_id;
    [[nodiscard]] auto append_output_point(output_record_id record, Point64 point)
        -> output_point_id;

    [[nodiscard]] auto contains(edge_id id) const noexcept -> bool;
    [[nodiscard]] auto contains(output_record_id id) const noexcept -> bool;
    [[nodiscard]] auto contains(output_point_id id) const noexcept -> bool;

    [[nodiscard]] auto edge(edge_id id) -> engine_edge_record&;
    [[nodiscard]] auto edge(edge_id id) const -> const engine_edge_record&;
    [[nodiscard]] auto output_ring(output_record_id id) const -> std::vector<Point64>;

    auto clear() noexcept -> void;

private:
    template <class T>
    struct slot {
        std::uint32_t generation{};
        T value{};
    };

    struct output_point_record {
        Point64 point{};
        output_point_id next{};
        output_point_id previous{};
    };

    struct output_record {
        output_point_id first{};
    };

    std::vector<slot<engine_edge_record>> edges_;
    std::vector<slot<output_point_record>> output_points_;
    std::vector<slot<output_record>> output_records_;
    std::uint32_t generation_ = 1;

    [[nodiscard]] auto next_generation() const noexcept -> std::uint32_t;
    [[nodiscard]] auto output_point(output_point_id id) -> output_point_record&;
    [[nodiscard]] auto output_point(output_point_id id) const -> const output_point_record&;
    [[nodiscard]] auto output_record_ref(output_record_id id) -> output_record&;
    [[nodiscard]] auto output_record_ref(output_record_id id) const -> const output_record&;
};

}  // namespace clipper2next::internal
