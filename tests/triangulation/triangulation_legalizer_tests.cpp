#include <gtest/gtest.h>

#include "triangulation/private/triangulation_legalizer.h"
#include "triangulation/private/triangulation_exact_predicates.h"

namespace next = clipper2next;

namespace {
auto prepared_result(next::Point64 a, next::Point64 b, next::Point64 c, next::Point64 d) {
    const std::array<next::Point64,4> points{a,b,c,d};
    const auto domain=next::internal::triangulation_predicate_domain::from_points(points.size(),
        [&](std::size_t i){return points[i];});
    return next::internal::oriented_in_circle(points[0],points[1],points[2],points[3],domain);
}
} // namespace

TEST(Clipper2NextTriangulationLegalizerTests, PredicatesClassifyTurnsAndInCircle) {
    EXPECT_TRUE(next::internal::LeftTurning({0, 0}, {0, 10}, {10, 0}));
    EXPECT_TRUE(next::internal::RightTurning({0, 0}, {10, 0}, {0, 10}));
    EXPECT_GT(prepared_result({0, 0}, {10, 0}, {0, 10}, {1, 1}).circle, 0);
}

TEST(Clipper2NextTriangulationLegalizerTests, NearCocircularDeterminantRetainsIntegerSign) {
    constexpr int64_t n = int64_t{1} << 27;
    EXPECT_GT(prepared_result({0, -n}, {-n, 0}, {0, n}, {n, 1}).circle, 0);
}
