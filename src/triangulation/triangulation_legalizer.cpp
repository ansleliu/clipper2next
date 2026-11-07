#include "triangulation/private/triangulation_legalizer.h"

#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {
auto LeftTurning(const Point64& p1, const Point64& p2, const Point64& p3) -> bool {
    return cross_product_sign_in_clipper_range(p1, p2, p3) < 0;
}

auto RightTurning(const Point64& p1, const Point64& p2, const Point64& p3) -> bool {
    return cross_product_sign_in_clipper_range(p1, p2, p3) > 0;
}

}  // namespace clipper2next::internal
