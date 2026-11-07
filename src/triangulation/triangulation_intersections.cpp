#include "triangulation/private/triangulation_intersections.h"

#include "geometry/private/geometry_predicates.h"

#include <algorithm>

namespace clipper2next::internal {

auto SegsIntersect(Point64 a, Point64 b, Point64 c, Point64 d) -> triangulation_intersect_kind {
    if (a == b || c == d || std::max(a.x, b.x) < std::min(c.x, d.x) ||
        std::max(c.x, d.x) < std::min(a.x, b.x) || std::max(a.y, b.y) < std::min(c.y, d.y) ||
        std::max(c.y, d.y) < std::min(a.y, b.y)) {
        return triangulation_intersect_kind::none;
    }
    const auto abc = cross_product_sign_in_clipper_range(a, b, c);
    const auto abd = cross_product_sign_in_clipper_range(a, b, d);
    if (abc == 0 && abd == 0) {
        const bool overlap = a.x != b.x ? std::max(std::min(a.x, b.x), std::min(c.x, d.x)) <
                                              std::min(std::max(a.x, b.x), std::max(c.x, d.x))
                                        : std::max(std::min(a.y, b.y), std::min(c.y, d.y)) <
                                              std::min(std::max(a.y, b.y), std::max(c.y, d.y));
        return overlap ? triangulation_intersect_kind::collinear
                       : triangulation_intersect_kind::none;
    }
    if (abc * abd >= 0) { return triangulation_intersect_kind::none; }
    const auto cda = cross_product_sign_in_clipper_range(c, d, a);
    const auto cdb = cross_product_sign_in_clipper_range(c, d, b);
    return cda * cdb < 0 ? triangulation_intersect_kind::intersect
                         : triangulation_intersect_kind::none;
}

}  // namespace clipper2next::internal
