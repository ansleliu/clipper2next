#include <gtest/gtest.h>
#include "clipper2next/core.h"
#include "geometry/private/geometry_segment_query.h"
#include "triangulation/private/triangulation_exact_predicates.h"
#include <array>

namespace next=clipper2next;
namespace in=next::internal;
namespace {
// Both proven narrow and complete arithmetic must produce the same facts.
auto facts(next::Point64 a,next::Point64 b,next::Point64 c,next::Point64 d) {
    const std::array<next::Point64,4> points{a,b,c,d};
    const auto domain=in::triangulation_predicate_domain::from_points(points.size(),
        [&](std::size_t i){return points[i];});
    const auto pack=[](auto value) {
        return std::array<bool,5>{value.first_on_target,value.second_on_target,
            value.target_first_on_query,value.target_second_on_query,value.proper_crossing};
    };
    const auto bounds=in::geometry_segment_query::segment_bounds(a,b);
    const auto target=in::geometry_segment_query::segment_bounds(c,d);
    const auto current=pack(in::geometry_segment_query{a,b,domain.orientation_fits_signed64(),bounds}.contacts(c,d,target));
    EXPECT_EQ(current,pack(in::geometry_segment_query{a,b,false}.contacts(c,d,target)));
    return current;
}
}

TEST(Clipper2NextSourceContactFactsTests, StrictCrossingHasNoEndpointContact) {
    EXPECT_EQ((facts({0,0},{10,10},{0,10},{10,0})),(std::array<bool,5>{false,false,false,false,true}));
}
TEST(Clipper2NextSourceContactFactsTests, LongerCollinearTargetContainsOnlyQueryEndpoints) {
    EXPECT_EQ((facts({0,0},{10,0},{-5,0},{15,0})),(std::array<bool,5>{true,true,false,false,false}));
    EXPECT_EQ((facts({-5,0},{15,0},{10,0},{0,0})),(std::array<bool,5>{false,false,true,true,false}));
}
TEST(Clipper2NextSourceContactFactsTests, ClosedEndpointAndTJunctionRemainDistinct) {
    EXPECT_EQ((facts({0,0},{10,0},{5,0},{5,10})),(std::array<bool,5>{false,false,true,false,false}));
    EXPECT_EQ((facts({0,0},{10,0},{10,0},{10,10})),(std::array<bool,5>{false,true,true,false,false}));
    EXPECT_EQ((facts({0,0},{10,0},{11,0},{20,0})),(std::array<bool,5>{false,false,false,false,false}));
}
TEST(Clipper2NextSourceContactFactsTests, DegeneratePointsAndCoincidentSegmentsUseClosedMembership) {
    EXPECT_EQ((facts({0,0},{0,0},{-1,0},{1,0})),(std::array<bool,5>{true,true,false,false,false}));
    EXPECT_EQ((facts({0,0},{10,0},{10,0},{0,0})),(std::array<bool,5>{true,true,true,true,false}));
    EXPECT_EQ((facts({0,0},{0,0},{0,0},{0,0})),(std::array<bool,5>{true,true,true,true,false}));
}
TEST(Clipper2NextSourceContactFactsTests, FullRangeAndLargeTranslationPreserveExactFacts) {
    constexpr auto m=next::MAX_COORD;
    EXPECT_EQ((facts({-m,-m},{m,m},{-m,m},{m,-m})),(std::array<bool,5>{false,false,false,false,true}));
    constexpr auto t=int64_t{1}<<59;
    EXPECT_EQ((facts({t,t},{t+10,t+10},{t,t+10},{t+10,t})),(std::array<bool,5>{false,false,false,false,true}));
    constexpr auto s=int64_t{1}<<32;
    EXPECT_EQ((facts({0,0},{s,s},{0,s},{s,0})),(std::array<bool,5>{false,false,false,false,true}));
}
