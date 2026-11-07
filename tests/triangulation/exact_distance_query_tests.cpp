#include "triangulation/private/triangulation_exact_predicates.h"
#include <gtest/gtest.h>

namespace next=clipper2next;
namespace in=next::internal;

TEST(Clipper2NextExactDistanceQueryTests, NativeBoundaryKeepsExactTwoAxisSum) {
    constexpr int64_t edge=(int64_t{1}<<31)-1;
    const in::exact_point_distance_query query{{0,0},{0,0,edge,edge}};
    EXPECT_EQ(query.to_point({edge,edge}).squared,
              (std::array<uint64_t,2>{uint64_t{0x7ffffffe00000002},0}));
    const in::exact_point_distance_query boundary{{0,0},{0,0,edge+1,edge+1}};
    EXPECT_EQ(boundary.to_point({edge+1,edge+1}).squared,
              (std::array<uint64_t,2>{uint64_t{1}<<63,0}));
}

TEST(Clipper2NextExactDistanceQueryTests, PrecisionBoundaryNeverDropsHighWord) {
    constexpr int64_t edge=(int64_t{1}<<32)-1;
    const in::exact_point_distance_query query{{0,0},{0,0,edge,edge}};
    EXPECT_EQ(query.to_point({edge,edge}).squared,
              (std::array<uint64_t,2>{uint64_t{0xfffffffc00000002},1}));
}

TEST(Clipper2NextExactDistanceQueryTests, OutsideQueryUsesActualDisplacement) {
    constexpr auto m=next::MAX_COORD;
    const next::Point64 point{-m,-m};
    const in::exact_point_distance_query query{{m,m},{-m,-m,-m+1,-m+1}};
    EXPECT_EQ(query.to_point(point).squared,
              in::squared_point_distance_in_clipper_range({m,m},point).squared);
    EXPECT_EQ(query.to_bounds({-m,-m,-m+1,-m+1}).squared,
              in::squared_point_distance_in_clipper_range({m,m},{-m+1,-m+1}).squared);
}

TEST(Clipper2NextExactDistanceQueryTests, BoxProjectionAndPointDistanceUseSameExactValue) {
    const in::exact_point_distance_query query{{7,-3},{-20,-20,20,20}};
    EXPECT_EQ(query.to_bounds({-1,0,3,10}).squared,
              (std::array<uint64_t,2>{25,0}));
    EXPECT_EQ(query.to_bounds({7,-3,7,-3}).squared,
              (std::array<uint64_t,2>{0,0}));
    EXPECT_EQ(query.to_point({3,0}).squared,query.to_bounds({-1,0,3,10}).squared);
}

TEST(Clipper2NextExactDistanceQueryTests, FullAdmittedRangeRetainsBothWords) {
    constexpr auto m=next::MAX_COORD;
    const in::exact_point_distance_query query{{m,-m},{-m,-m,m,m}};
    for(const next::Point64 point:std::array<next::Point64,4>{{{-m,m},{-m,-m},{m,m},{0,0}}}) {
        EXPECT_EQ(query.to_point(point).squared,
                  in::squared_point_distance_in_clipper_range({m,-m},point).squared);
    }
    EXPECT_EQ(query.to_bounds({-m,-m,m,m}).squared,(std::array<uint64_t,2>{0,0}));
}
