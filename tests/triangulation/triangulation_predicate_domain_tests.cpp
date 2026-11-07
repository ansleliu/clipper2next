#include <gtest/gtest.h>
#include "triangulation/private/triangulation_exact_predicates.h"
#include <array>

namespace next=clipper2next;
namespace in=next::internal;
namespace {
auto domain(const std::array<next::Point64,4>& points) {
    return in::triangulation_predicate_domain::from_points(points.size(),[&](std::size_t i){return points[i];});
}
}
TEST(Clipper2NextTriangulationPredicateDomainTests, ExactExtentBoundariesSelectProvenPrecision) {
    for(const int64_t span:{(int64_t{1}<<14)-1,int64_t{1}<<14,(int64_t{1}<<30)-1,int64_t{1}<<30}) {
        const std::array<next::Point64,4> points{{{0,0},{span,0},{0,span},{1,1}}};
        const auto prepared=domain(points);
        const auto expected=span<(int64_t{1}<<14) ? in::circle_precision::exact64 :
            span<(int64_t{1}<<30) ? in::circle_precision::exact128 : in::circle_precision::exact256;
        EXPECT_EQ(prepared.precision(),expected);
        const auto result=in::oriented_in_circle(points[0],points[1],points[2],points[3],prepared);
        EXPECT_EQ(result.circle,1);EXPECT_EQ(result.orientation,1);
    }
}
TEST(Clipper2NextTriangulationPredicateDomainTests, ExtremeTranslationKeepsTheSameExactSmallDomain) {
    constexpr auto m=next::MAX_COORD;
    const std::array<next::Point64,4> points{{{m-100,m-100},{m,m-100},{m-100,m},{m-99,m-99}}};
    const auto prepared=domain(points);EXPECT_EQ(prepared.precision(),in::circle_precision::exact64);
    const auto result=in::oriented_in_circle(points[0],points[1],points[2],points[3],prepared);
    EXPECT_EQ(result.circle,1);EXPECT_EQ(result.orientation,1);
}
TEST(Clipper2NextTriangulationPredicateDomainTests, WiderWholeGraphDoesNotBorrowAQuadOnlyBound) {
    constexpr auto m=next::MAX_COORD;
    const std::array<next::Point64,6> points{{{0,0},{10,0},{0,10},{1,1},{-m,-m},{m,m}}};
    const auto prepared=in::triangulation_predicate_domain::from_points(points.size(),[&](std::size_t i){return points[i];});
    EXPECT_EQ(prepared.precision(),in::circle_precision::exact256);
    const auto result=in::oriented_in_circle(points[0],points[1],points[2],points[3],prepared);
    EXPECT_EQ(result.circle,1);EXPECT_EQ(result.orientation,1);
}
TEST(Clipper2NextTriangulationPredicateDomainTests, EmptyAndSingletonPreparationHaveExplicitMeaning) {
    const auto empty=in::triangulation_predicate_domain::from_points(0,[](std::size_t){return next::Point64{};});
    EXPECT_EQ(empty.point_count(),0U);EXPECT_EQ(empty.precision(),in::circle_precision::exact256);
    const auto single=in::triangulation_predicate_domain::from_points(1,[](std::size_t){return next::Point64{7,9};});
    EXPECT_EQ(single.point_count(),1U);EXPECT_EQ(single.precision(),in::circle_precision::exact64);
    EXPECT_TRUE(single.contains({7,9}));EXPECT_FALSE(single.contains({8,9}));
}
TEST(Clipper2NextTriangulationPredicateDomainTests, PreparedValueDoesNotRetainSourceGetterOrStorage) {
    const auto prepared=[] {
        const std::array<next::Point64,4> points{{{0,0},{10,0},{0,10},{1,1}}};
        return domain(points);
    }();
    const auto result=in::oriented_in_circle({0,0},{10,0},{0,10},{1,1},prepared);
    EXPECT_EQ(result.circle,1);EXPECT_EQ(result.orientation,1);
}
TEST(Clipper2NextTriangulationPredicateDomainTests, PreparedAndExpandedSnapshotsRemainIndependent) {
    const std::array<next::Point64,4> small{{{0,0},{10,0},{0,10},{1,1}}};
    auto wide=small;wide[2]={int64_t{1}<<31,int64_t{1}<<31};
    const auto a=domain(small),b=domain(wide);
    EXPECT_EQ(a.precision(),in::circle_precision::exact64);EXPECT_EQ(b.precision(),in::circle_precision::exact256);
    EXPECT_TRUE(a.contains({10,0}));EXPECT_FALSE(a.contains(wide[2]));EXPECT_TRUE(b.contains(wide[2]));
}
