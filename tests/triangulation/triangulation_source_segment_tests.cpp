#include "triangulation/private/triangulation_source_segment.h"
#include <gtest/gtest.h>
#include <vector>

namespace next=clipper2next;
namespace in=next::internal;

namespace {
auto points(const in::triangulation_source_segment& segment)->next::Path64 {
    const auto values=segment.points();return {values.begin(),values.end()};
}
}

TEST(Clipper2NextTriangulationSourceSegmentTests, OriginalEndpointsAndRingHaveOwnedIdentity) {
    const in::triangulation_source_segment segment{{0,0},{10,10},7};
    EXPECT_EQ(segment.first(),next::Point64(0,0));
    EXPECT_EQ(segment.second(),next::Point64(10,10));
    EXPECT_EQ(segment.ring(),7U);
    EXPECT_EQ(points(segment),(next::Path64{{0,0},{10,10}}));
}
TEST(Clipper2NextTriangulationSourceSegmentTests, EndpointContactsDoNotBecomeInteriorCuts) {
    in::triangulation_source_segment segment{{0,0},{10,0},0};
    for(unsigned i=0;i<100;++i){segment.add_split({0,0});segment.add_split({10,0});}
    segment.order_splits();
    EXPECT_EQ(points(segment),(next::Path64{{0,0},{10,0}}));
}
TEST(Clipper2NextTriangulationSourceSegmentTests, InteriorCutsKeepOriginalDirectionAndUniqueness) {
    in::triangulation_source_segment segment{{10,0},{0,0},2};
    for(const auto p:next::Path64{{6,0},{2,0},{8,0},{8,0},{0,0},{10,0}}){segment.add_split(p);}
    segment.order_splits();
    EXPECT_EQ(points(segment),(next::Path64{{10,0},{8,0},{6,0},{2,0},{0,0}}));
    EXPECT_EQ(segment.ring(),2U);
}
TEST(Clipper2NextTriangulationSourceSegmentTests, FullAdmittedRangeHasExactSplitOrder) {
    constexpr auto m=next::MAX_COORD;
    in::triangulation_source_segment segment{{-m,-m},{m,m},1};
    segment.add_split({0,0});segment.add_split({0,0});segment.order_splits();
    EXPECT_EQ(points(segment),(next::Path64{{-m,-m},{0,0},{m,m}}));
}
TEST(Clipper2NextTriangulationSourceSegmentTests, SegmentDoesNotBorrowCallerPoints) {
    auto segment=[]{next::Point64 a{0,10},b{0,-10};return in::triangulation_source_segment{a,b,4};}();
    segment.add_split({0,2});segment.add_split({0,6});segment.order_splits();
    EXPECT_EQ(points(segment),(next::Path64{{0,10},{0,6},{0,2},{0,-10}}));
}
