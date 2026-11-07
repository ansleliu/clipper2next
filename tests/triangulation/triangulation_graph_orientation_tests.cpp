#include <gtest/gtest.h>
#include "triangulation/private/triangulation_exact_predicates.h"
#include "geometry/private/geometry_predicates.h"
#include <array>

namespace next=clipper2next;
namespace in=next::internal;
namespace {
auto graph_orientation(next::Point64 a,next::Point64 b,next::Point64 c,
                     const in::triangulation_predicate_domain& domain) -> int {
    EXPECT_TRUE(domain.contains(a));EXPECT_TRUE(domain.contains(b));EXPECT_TRUE(domain.contains(c));
    return domain.orientation(a,b,c);
}
template<std::size_t N>
auto prepare(const std::array<next::Point64,N>& points) {
    return in::triangulation_predicate_domain::from_points(points.size(),[&](std::size_t i){return points[i];});
}
}

TEST(Clipper2NextGraphOrientationContractTests, ExactOrientationAtNumericExtentBoundaries) {
    for(const int64_t extent : {(int64_t{1}<<31)-1,int64_t{1}<<31,(int64_t{1}<<32)-1}) {
        const std::array<next::Point64,4> points{{{0,0},{extent,0},{0,extent},{extent,extent}}};
        const auto domain=prepare(points);
        EXPECT_EQ(graph_orientation(points[0],points[1],points[2],domain),1);
        EXPECT_EQ(graph_orientation(points[0],points[2],points[1],domain),-1);
        EXPECT_EQ(graph_orientation(points[0],points[1],points[1],domain),0);
    }
}

TEST(Clipper2NextGraphOrientationContractTests, ExtremeTranslationKeepsUnitAreaAndCollinearity) {
    constexpr auto m=next::MAX_COORD;
    const std::array<next::Point64,4> points{{{m-2,m-2},{m-1,m-2},{m-2,m-1},{m,m-2}}};
    const auto domain=prepare(points);
    EXPECT_EQ(graph_orientation(points[0],points[1],points[2],domain),1);
    EXPECT_EQ(graph_orientation(points[0],points[2],points[1],domain),-1);
    EXPECT_EQ(graph_orientation(points[0],points[1],points[3],domain),0);
}

TEST(Clipper2NextGraphOrientationContractTests, WholeWideDomainPreservesSmallAndMaxRangeTriples) {
    constexpr auto m=next::MAX_COORD;
    const std::array<next::Point64,6> points{{{-m,-m},{m,-m},{-m,m},{0,0},{1,0},{0,1}}};
    const auto domain=prepare(points);
    EXPECT_EQ(graph_orientation(points[0],points[1],points[2],domain),1);
    EXPECT_EQ(graph_orientation(points[0],points[2],points[1],domain),-1);
    EXPECT_EQ(graph_orientation(points[3],points[4],points[5],domain),1);
}

TEST(Clipper2NextGraphOrientationContractTests, SnapshotDoesNotRetainGetterAndSingletonIsCollinear) {
    std::size_t calls=0;
    const std::array<next::Point64,3> points{{{0,0},{5,0},{0,5}}};
    const auto domain=in::triangulation_predicate_domain::from_points(points.size(),[&](std::size_t i){++calls;return points[i];});
    ASSERT_EQ(calls,points.size());
    for(unsigned i=0;i<32;++i)EXPECT_EQ(graph_orientation(points[0],points[1],points[2],domain),1);
    EXPECT_EQ(calls,points.size());
    const auto single=in::triangulation_predicate_domain::from_points(1,[](std::size_t){return next::Point64{7,9};});
    EXPECT_EQ(graph_orientation({7,9},{7,9},{7,9},single),0);
}
