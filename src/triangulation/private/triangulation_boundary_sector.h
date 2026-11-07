#pragma once
#include "triangulation/private/triangulation_boundary_geometry.h"
#include <utility>

namespace clipper2next::internal {

// Numeric facts own their value; the source event and graph may then disappear.
class boundary_sector final {
public:
    boundary_sector(Point64 origin, Point64 outgoing, Point64 incoming) noexcept
        : origin_(origin), start_(ray_to(origin_, outgoing)),
          end_(ray_to(origin_, incoming)), ordered_(ray_less(start_, end_)) {}
    [[nodiscard]] auto contains_ray(boundary_ray ray, bool include_boundary = true) const noexcept -> bool {
        if (same_boundary_ray(start_, ray) || same_boundary_ray(end_, ray)) { return include_boundary; }
        return ordered_ ? ray_less(start_, ray) && ray_less(ray, end_)
                        : ray_less(start_, ray) || ray_less(ray, end_);
    }
    [[nodiscard]] auto contains_triangle(Point64 a, Point64 b) const noexcept -> bool {
        auto ra = ray_to(origin_, a), rb = ray_to(origin_, b);
        const auto cross = ray_cross(ra, rb);
        if (!cross) { return false; }
        if (cross < 0) { std::swap(ra, rb); }
        if (!contains_ray(ra) || !contains_ray(rb)) { return false; }
        // Both endpoints can be in a reflex sector while the minor wedge fills its hole.
        if (same_boundary_ray(start_, ra)) { return true; }
        if (same_boundary_ray(start_, rb)) { return false; }
        const bool crosses_start = ray_less(ra, rb) ? ray_less(ra, start_) && ray_less(start_, rb)
                                                   : ray_less(ra, start_) || ray_less(start_, rb);
        return !crosses_start;
    }
private:
    const Point64 origin_;
    const boundary_ray start_;
    const boundary_ray end_;
    const bool ordered_;
};

}  // namespace clipper2next::internal
