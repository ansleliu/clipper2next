#pragma once
#include "clipper2next/core.h"
#include "geometry/private/geometry_exact.h"

namespace clipper2next::internal {
struct boundary_ray {
    int64_t x;
    int64_t y;
};
// Components are differences/sums of admitted coordinates (<2^63). Each
// product fits 128 unsigned bits. Comparing signed products avoids overflow in
// their difference and does not mis-admit probe vectors as public points.
[[nodiscard]] inline auto ray_cross(boundary_ray a, boundary_ray b) noexcept -> int {
    return geometry_exact::compare_signed_products(a.x, b.y, a.y, b.x);
}
[[nodiscard]] inline auto boundary_ray_upper(boundary_ray ray) noexcept -> bool {
    return ray.y > 0 || (ray.y == 0 && ray.x > 0);
}
[[nodiscard]] inline auto ray_less(boundary_ray a, boundary_ray b) noexcept -> bool {
    if (boundary_ray_upper(a) != boundary_ray_upper(b)) { return boundary_ray_upper(a); }
    return ray_cross(a, b) > 0;
}
[[nodiscard]] inline auto same_boundary_ray(boundary_ray a, boundary_ray b) noexcept -> bool {
    return boundary_ray_upper(a) == boundary_ray_upper(b) && ray_cross(a, b) == 0;
}
[[nodiscard]] inline auto sweep_before(Point64 a, Point64 b) noexcept -> bool {
    return a.y > b.y || (a.y == b.y && a.x < b.x);
}
[[nodiscard]] inline auto ray_to(Point64 a, Point64 b) noexcept -> boundary_ray {
    return {b.x - a.x, b.y - a.y};
}
}  // namespace clipper2next::internal
