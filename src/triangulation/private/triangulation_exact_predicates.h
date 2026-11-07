#pragma once

#include "clipper2next/core.h"
#include "geometry/private/geometry_predicates.h"
#include <cassert>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace clipper2next::internal {

// Internal predicates require coordinates in [-MAX_COORD, MAX_COORD].
struct exact_point_distance final {
    std::array<std::uint64_t, 2> squared{};
};

enum class circle_precision { exact64, exact128, exact256 };

// A numeric extent snapshot; preparation borrows no point storage or callback.
class triangulation_predicate_domain final {
    Rect64 bounds_{};
    std::size_t point_count_{};
    circle_precision precision_ = circle_precision::exact256;
    bool orientation64_ = false;
public:
    template <typename PointAt>
    [[nodiscard]] static auto from_points(std::size_t count, PointAt&& point_at)
        -> triangulation_predicate_domain {
        triangulation_predicate_domain result;
        if (!count) { return result; }
        const Point64 first = point_at(0);
        result.bounds_ = {first.x, first.y, first.x, first.y};
        result.point_count_ = count;
        for (std::size_t i = 1; i < count; ++i) {
            const Point64 point = point_at(i);
            result.bounds_.left = std::min(result.bounds_.left, point.x);
            result.bounds_.right = std::max(result.bounds_.right, point.x);
            result.bounds_.top = std::min(result.bounds_.top, point.y);
            result.bounds_.bottom = std::max(result.bounds_.bottom, point.y);
        }
        const auto span = static_cast<uint64_t>(result.bounds_.right-result.bounds_.left) |
                          static_cast<uint64_t>(result.bounds_.bottom-result.bounds_.top);
        result.orientation64_ = span < (uint64_t{1}<<31);
        result.precision_ = span < (uint64_t{1}<<14) ? circle_precision::exact64 :
                            span < (uint64_t{1}<<30) ? circle_precision::exact128 : circle_precision::exact256;
        return result;
    }
    // Every consumed point belongs to this complete numeric snapshot.
    [[nodiscard]] auto orientation(Point64 a, Point64 b, Point64 c) const noexcept -> int {
        assert(contains(a) && contains(b) && contains(c));
        if (!orientation64_) { return cross_product_sign_in_clipper_range(a,b,c); }
        const auto value=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        return (value>0)-(value<0);
    }
    [[nodiscard]] auto orientation_fits_signed64() const noexcept -> bool { return orientation64_; }
    [[nodiscard]] auto precision() const noexcept -> circle_precision { return precision_; }
    [[nodiscard]] auto point_count() const noexcept -> std::size_t { return point_count_; }
    [[nodiscard]] auto bounds() const noexcept -> const Rect64& { return bounds_; }
    [[nodiscard]] auto contains(Point64 point) const noexcept -> bool {
        return point_count_ && point.x >= bounds_.left && point.x <= bounds_.right &&
               point.y >= bounds_.top && point.y <= bounds_.bottom;
    }
};

struct oriented_in_circle_result final {
    int circle{};
    int orientation{};
};
[[nodiscard]] auto oriented_in_circle(const Point64& a, const Point64& b,
                                      const Point64& c, const Point64& d,
                                      const triangulation_predicate_domain& domain) noexcept
    -> oriented_in_circle_result;
[[nodiscard]] auto squared_point_distance_in_clipper_range(Point64 a, Point64 b) noexcept
    -> exact_point_distance;
[[nodiscard]] auto compare_squared_distance(const exact_point_distance& a,
                                            const exact_point_distance& b) noexcept -> int;

// One query proves all displacements to its actual immutable directory root.
// to_point and to_bounds consume points/boxes from that root, not arbitrary data.
class exact_point_distance_query final {
public:
    exact_point_distance_query(Point64 query, const Rect64& root) noexcept
        : query_(query), narrow_((magnitude(root.left-query.x) | magnitude(root.right-query.x) |
                                 magnitude(root.top-query.y) | magnitude(root.bottom-query.y)) <
                                (uint64_t{1}<<31)) {}
    [[nodiscard]] auto to_point(Point64 point) const noexcept -> exact_point_distance {
        if (!narrow_) { return squared_point_distance_in_clipper_range(query_, point); }
        const auto dx = point.x-query_.x, dy = point.y-query_.y;
        // Each displacement <2^31: both squares and their sum fit signed64.
        return {{static_cast<uint64_t>(dx*dx+dy*dy), 0U}};
    }
    [[nodiscard]] auto to_bounds(const Rect64& box) const noexcept -> exact_point_distance {
        return to_point({std::clamp(query_.x, box.left, box.right),
                         std::clamp(query_.y, box.top, box.bottom)});
    }
private:
    static auto magnitude(int64_t value) noexcept -> uint64_t {
        return value < 0 ? uint64_t{0}-static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    }
    const Point64 query_;
    const bool narrow_;
};

}  // namespace clipper2next::internal
