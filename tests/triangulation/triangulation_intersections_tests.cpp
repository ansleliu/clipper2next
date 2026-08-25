#include <gtest/gtest.h>

#include "triangulation/private/triangulation_intersections.h"

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

TEST(Clipper2NextTriangulationIntersectionsTests,
     ShortestDistanceFromSegmentHandlesInteriorAndEndpoint) {
    EXPECT_DOUBLE_EQ(next::internal::ShortestDistFromSegment({5, 5}, {0, 0}, {10, 0}), 25.0);
    EXPECT_DOUBLE_EQ(next::internal::ShortestDistFromSegment({-3, 4}, {0, 0}, {10, 0}), 25.0);
}
