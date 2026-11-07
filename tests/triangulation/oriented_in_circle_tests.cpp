#include <gtest/gtest.h>
#include "triangulation/private/triangulation_exact_predicates.h"

namespace next=clipper2next;
namespace in=next::internal;

namespace {
auto prepared_result(next::Point64 a, next::Point64 b, next::Point64 c, next::Point64 d) {
    const std::array<next::Point64,4> points{a,b,c,d};
    const auto domain=next::internal::triangulation_predicate_domain::from_points(points.size(),
        [&](std::size_t i){return points[i];});
    return next::internal::oriented_in_circle(points[0],points[1],points[2],points[3],domain);
}
} // namespace

TEST(Clipper2NextOrientedInCircleTests, InteriorClassificationIncludesTheTriangleOrientation) {
    const auto result=prepared_result({0,0},{10,0},{0,10},{1,1});
    EXPECT_EQ(result.circle,1);EXPECT_EQ(result.orientation,1);
    const auto reverse=prepared_result({10,0},{0,0},{0,10},{1,1});
    EXPECT_EQ(reverse.circle,-1);EXPECT_EQ(reverse.orientation,-1);
}
TEST(Clipper2NextOrientedInCircleTests, CocircularZeroRetainsNonzeroOrientation) {
    constexpr int64_t n=int64_t{1}<<29;
    const auto result=prepared_result({0,0},{n,0},{n,n},{0,n});
    EXPECT_EQ(result.circle,0);EXPECT_EQ(result.orientation,1);
}
TEST(Clipper2NextOrientedInCircleTests, CollinearTriangleDoesNotEraseTheFourPointDeterminant) {
    const auto result=prepared_result({0,0},{1,0},{2,0},{0,1});
    EXPECT_NE(result.circle,0);EXPECT_EQ(result.orientation,0);
}
TEST(Clipper2NextOrientedInCircleTests, NearCocircularCancellationKeepsBothIndependentSigns) {
    constexpr int64_t n=int64_t{1}<<27;
    const auto result=prepared_result({0,-n},{-n,0},{0,n},{n,1});
    EXPECT_EQ(result.circle,1);EXPECT_EQ(result.orientation,-1);
}
TEST(Clipper2NextOrientedInCircleTests, PrecisionBoundariesKeepTheSameIntegerClassification) {
    for(const int64_t n:{(int64_t{1}<<14)-1,int64_t{1}<<14,(int64_t{1}<<14)+1,
                         (int64_t{1}<<30)-1,int64_t{1}<<30,(int64_t{1}<<30)+1}) {
        const auto result=prepared_result({0,0},{n,0},{0,n},{1,1});
        EXPECT_EQ(result.circle,1);EXPECT_EQ(result.orientation,1);
    }
}
TEST(Clipper2NextOrientedInCircleTests, AdmittedWideCoordinatesAndExtremeTranslationRemainExact) {
    constexpr auto m=next::MAX_COORD;
    const auto wide=prepared_result({-m,-m},{m,-m},{m,m},{-m,m-1});
    EXPECT_EQ(wide.circle,1);EXPECT_EQ(wide.orientation,1);
    for(const int64_t shift:{m-100,-m}) {
        const auto translated=prepared_result({shift,shift},{shift+100,shift},
            {shift,shift+100},{shift+50,shift+50});
        EXPECT_EQ(translated.circle,1);EXPECT_EQ(translated.orientation,1);
    }
}
