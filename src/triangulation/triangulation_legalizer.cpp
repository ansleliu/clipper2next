#include "triangulation/private/triangulation_legalizer.h"

namespace clipper2next::internal {
namespace {

struct lifted_point final {
    double x = 0.0;
    double y = 0.0;
    double radius_squared = 0.0;
};

[[nodiscard]] auto lift_relative_to(const Point64& point, const Point64& origin) -> lifted_point {
    const auto x = static_cast<double>(point.x - origin.x);
    const auto y = static_cast<double>(point.y - origin.y);
    return lifted_point{x, y, x * x + y * y};
}

[[nodiscard]] auto determinant(const lifted_point& first,
                               const lifted_point& second,
                               const lifted_point& third) -> double {
    const auto second_third_minor =
        second.y * third.radius_squared - third.y * second.radius_squared;
    const auto first_third_minor = first.y * third.radius_squared - third.y * first.radius_squared;
    const auto first_second_minor =
        first.y * second.radius_squared - second.y * first.radius_squared;
    return first.x * second_third_minor - second.x * first_third_minor +
           third.x * first_second_minor;
}

}  // namespace

auto LeftTurning(const Point64& p1, const Point64& p2, const Point64& p3) -> bool {
    return cross_product_sign(p1, p2, p3) < 0;
}

auto RightTurning(const Point64& p1, const Point64& p2, const Point64& p3) -> bool {
    return cross_product_sign(p1, p2, p3) > 0;
}

auto InCircleTest(const Point64& ptA, const Point64& ptB, const Point64& ptC, const Point64& ptD)
    -> double {
    return determinant(
        lift_relative_to(ptA, ptD), lift_relative_to(ptB, ptD), lift_relative_to(ptC, ptD));
}

}  // namespace clipper2next::internal
