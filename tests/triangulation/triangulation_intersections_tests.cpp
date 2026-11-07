#include <gtest/gtest.h>

#include "triangulation/private/triangulation_intersections.h"
#include "triangulation/private/triangulation_exact_predicates.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationIntersectionsTests,
     SegmentIntersectionClassifiesCrossingAndCollinear) {
    EXPECT_EQ(next::internal::SegsIntersect({0, 0}, {10, 10}, {0, 10}, {10, 0}),
              next::internal::triangulation_intersect_kind::intersect);
    EXPECT_EQ(next::internal::SegsIntersect({0, 0}, {10, 0}, {5, 0}, {15, 0}),
              next::internal::triangulation_intersect_kind::collinear);
    EXPECT_EQ(next::internal::SegsIntersect({0, 0}, {10, 0}, {10, 0}, {10, 10}),
              next::internal::triangulation_intersect_kind::none);
}

TEST(Clipper2NextTriangulationIntersectionsTests, NarrowCrossingRetainsIntegerSign) {
    for (const auto n : {int64_t{1} << 30, next::MAX_COORD}) {
        EXPECT_EQ(next::internal::SegsIntersect({0, 0}, {n, n - 1}, {1, 1}, {n - 1, n - 2}),
                  next::internal::triangulation_intersect_kind::intersect);
    }
}
TEST(Clipper2NextTriangulationIntersectionsTests, NearEqualAdmittedPointDistancesRemainOrdered) {
    constexpr auto m = next::MAX_COORD;
    const auto first = next::internal::squared_point_distance_in_clipper_range({0, 0}, {m, 0});
    const auto second = next::internal::squared_point_distance_in_clipper_range({0, 0}, {m, 1});
    EXPECT_LT(next::internal::compare_squared_distance(first, second), 0);
    EXPECT_GT(next::internal::compare_squared_distance(second, first), 0);
    EXPECT_EQ(next::internal::compare_squared_distance(first, first), 0);
}
