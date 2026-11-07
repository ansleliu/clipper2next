#pragma once
#include "clipper2next/core.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <span>
#include <vector>

namespace clipper2next::internal {

// The source endpoints always exist; extra storage represents actual interior cuts.
class triangulation_source_segment final {
public:
    triangulation_source_segment(Point64 first, Point64 second, std::size_t ring)
        : endpoints_{first, second}, ring_(ring) {}
    [[nodiscard]] auto first() const noexcept -> const Point64& { return endpoints_[0]; }
    [[nodiscard]] auto second() const noexcept -> const Point64& { return endpoints_[1]; }
    [[nodiscard]] auto ring() const noexcept -> std::size_t { return ring_; }
    [[nodiscard]] auto points() const noexcept -> std::span<const Point64> {
        return split_points_.empty() ? std::span<const Point64>{endpoints_}
                                     : std::span<const Point64>{split_points_};
    }
    auto add_split(Point64 point) -> void {
        if (point == first() || point == second()) { return; }
        if (split_points_.empty()) {
            split_points_.reserve(3U);
            split_points_.assign(endpoints_.begin(), endpoints_.end());
        }
        split_points_.push_back(point);
    }
    auto order_splits() -> void {
        if (split_points_.empty()) { return; }
        const auto dx = second().x-first().x, dy = second().y-first().y;
        const bool use_x = std::abs(dx) >= std::abs(dy);
        std::sort(split_points_.begin(), split_points_.end(), [&](Point64 a, Point64 b) {
            return use_x ? (dx > 0 ? a.x < b.x : a.x > b.x) : (dy > 0 ? a.y < b.y : a.y > b.y);
        });
        split_points_.erase(std::unique(split_points_.begin(), split_points_.end()), split_points_.end());
    }
private:
    std::array<Point64, 2> endpoints_;
    std::size_t ring_;
    std::vector<Point64> split_points_;
};

}  // namespace clipper2next::internal
