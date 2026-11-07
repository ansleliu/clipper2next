#pragma once
#include "geometry/private/geometry_exact.h"

#include "clipper2next/core/rect.h"
#include "geometry/private/geometry_predicates.h"
#include <algorithm>

namespace clipper2next::internal {

// All points are admitted; signed64 requires one common extent smaller than 2^31.
// Endpoint contact and proper crossing share the same exact side facts.
class geometry_segment_query final {
public:
    geometry_segment_query(Point64 first, Point64 second, bool signed64) noexcept
        : geometry_segment_query(first, second, signed64, segment_bounds(first, second)) {}
    geometry_segment_query(Point64 first, Point64 second, bool signed64, const Rect64& bounds) noexcept
        : first_(first), second_(second), dx_(second.x-first.x), dy_(second.y-first.y),
          bounds_(bounds), signed64_(signed64) {}

    [[nodiscard]] static auto segment_bounds(Point64 first, Point64 second) noexcept -> Rect64 {
        return {std::min(first.x,second.x),std::min(first.y,second.y),
                std::max(first.x,second.x),std::max(first.y,second.y)};
    }
    [[nodiscard]] auto overlaps(const Rect64& target) const noexcept -> bool {
        return bounds_.left<=target.right && target.left<=bounds_.right &&
               bounds_.top<=target.bottom && target.top<=bounds_.bottom;
    }

    [[nodiscard]] auto on_segment(Point64 point) const noexcept -> bool {
        return within_bounds(point) && side(point)==0;
    }
    // A target endpoint on the closed query or a strict crossing blocks it.
    // This does not treat a query inside a longer collinear target as a crossing.
    [[nodiscard]] auto endpoint_or_crossing(Point64 first, Point64 second) const noexcept -> bool {
        if (!overlaps(segment_bounds(first,second))) { return false; }
        const auto first_side=side(first);
        if (first_side==0 && within_bounds(first)) { return true; }
        const auto second_side=side(second);
        if (second_side==0 && within_bounds(second)) { return true; }
        if (first_side*second_side>=0) { return false; }
        return target_side(first,second,first_) * target_side(first,second,second_) < 0;
    }

    struct contacts_result final {
        bool first_on_target;
        bool second_on_target;
        bool target_first_on_query;
        bool target_second_on_query;
        bool proper_crossing;
    };

    // Source normalization consumes all endpoint contacts and strict crossing.
    // The caller owns the target's exact endpoint bounds and any broad-phase
    // rejection. Closed membership remains defined even for disjoint boxes.
    [[nodiscard]] auto contacts(Point64 first, Point64 second, const Rect64& target) const noexcept -> contacts_result {
        const auto inside_target=[&](Point64 point) {
            return target.left<=point.x && point.x<=target.right &&
                   target.top<=point.y && point.y<=target.bottom;
        };
        const bool first_at_first=first==first_, first_at_second=first==second_;
        const bool second_at_first=second==first_, second_at_second=second==second_;
        if (first_at_first || first_at_second || second_at_first || second_at_second) {
            // A shared endpoint rules out strict crossing. One remaining side
            // decides collinearity, including point segments and retraces.
            if (side(first_at_first || first_at_second ? second : first)!=0) {
                return {first_at_first || second_at_first,first_at_second || second_at_second,
                        first_at_first || first_at_second,second_at_first || second_at_second,false};
            }
            return {inside_target(first_),inside_target(second_),
                    within_bounds(first),within_bounds(second),false};
        }
        const auto first_side=side(first),second_side=side(second);
        if (first_side*second_side>0) { return {}; }
        const auto query_first_side=target_side(first,second,first_);
        const auto query_second_side=target_side(first,second,second_);
        return {query_first_side==0 && inside_target(first_),
                query_second_side==0 && inside_target(second_),
                first_side==0 && within_bounds(first),
                second_side==0 && within_bounds(second),
                first_side*second_side<0 && query_first_side*query_second_side<0};
    }

private:
    [[nodiscard]] auto within_bounds(Point64 point) const noexcept -> bool {
        return point.x>=bounds_.left && point.x<=bounds_.right &&
               point.y>=bounds_.top && point.y<=bounds_.bottom;
    }
    [[nodiscard]] auto side(Point64 point) const noexcept -> int {
        if (signed64_) {
            const auto value=dx_*(point.y-second_.y)-dy_*(point.x-second_.x);
            return (value>0)-(value<0);
        }
        return geometry_exact::compare_signed_products(dx_,point.y-second_.y,
                                                      dy_,point.x-second_.x);
    }
    [[nodiscard]] auto target_side(Point64 a, Point64 b, Point64 p) const noexcept -> int {
        if (!signed64_) { return cross_product_sign_in_clipper_range(a,b,p); }
        const auto value=(b.x-a.x)*(p.y-b.y)-(b.y-a.y)*(p.x-b.x);
        return (value>0)-(value<0);
    }
    const Point64 first_,second_;
    const int64_t dx_,dy_;
    const Rect64 bounds_;
    const bool signed64_;
};

}  // namespace clipper2next::internal
