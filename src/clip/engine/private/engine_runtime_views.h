// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clip/engine/private/engine_types.h"

#include <functional>
#include <vector>

namespace clipper2next::internal {

class active_edge_view final {
public:
    explicit active_edge_view(active_edge_node& edge) noexcept
        : edge_(edge) {}

    [[nodiscard]] auto bottom() const noexcept -> Point64 { return edge_.get().bottom; }
    [[nodiscard]] auto top() const noexcept -> Point64 { return edge_.get().top_point; }
    [[nodiscard]] auto current_x() const noexcept -> int64_t { return edge_.get().current_x; }
    [[nodiscard]] auto wind_delta() const noexcept -> int { return edge_.get().wind_dx; }
    [[nodiscard]] auto has_output() const noexcept -> bool { return edge_.get().has_output(); }

private:
    std::reference_wrapper<active_edge_node> edge_;
};

class output_ring_view final {
public:
    explicit output_ring_view(output_point_node& first) noexcept
        : first_(first) {}

    [[nodiscard]] auto is_isolated() const noexcept -> bool {
        return first_.get().is_isolated_ring();
    }

    [[nodiscard]] auto points() const -> std::vector<Point64> {
        std::vector<Point64> result;
        const auto& first = first_.get();
        auto current = &first;
        do {
            result.push_back(current->pt);
            current = current->next;
        } while (current != &first);
        return result;
    }

private:
    std::reference_wrapper<output_point_node> first_;
};

class local_minimum_view final {
public:
    explicit local_minimum_view(local_minimum_node& minimum) noexcept
        : minimum_(minimum) {}

    [[nodiscard]] auto point() const noexcept -> Point64 { return minimum_.get().vertex.get().pt; }
    [[nodiscard]] auto path_type() const noexcept -> PathType { return minimum_.get().polytype; }
    [[nodiscard]] auto is_open_path() const noexcept -> bool {
        return minimum_.get().is_open_path();
    }

private:
    std::reference_wrapper<local_minimum_node> minimum_;
};

}  // namespace clipper2next::internal
