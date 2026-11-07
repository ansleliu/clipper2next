#include <gtest/gtest.h>
#include "triangulation/private/triangulation_catalog.h"
#include <algorithm>
#include <random>
#include <vector>

namespace next=clipper2next;
namespace in=next::internal;
namespace {
using pair=std::pair<std::size_t,std::size_t>;
auto collect(const std::vector<next::Rect64>& boxes) {
    in::triangulation_bounds_catalog catalog;
    catalog.build(boxes.size(),[&](std::size_t i){return boxes[i];});
    std::vector<pair> actual,expected;
    EXPECT_TRUE(catalog.query_pairs([&](std::size_t i,std::size_t j) {
        EXPECT_LT(i,j);actual.emplace_back(i,j);return true;
    }));
    for(std::size_t i=0;i<boxes.size();++i)for(std::size_t j=i+1;j<boxes.size();++j) {
        const auto a=boxes[i],b=boxes[j];
        if(a.left<=b.right && b.left<=a.right && a.top<=b.bottom && b.top<=a.bottom)expected.emplace_back(i,j);
    }
    std::sort(actual.begin(),actual.end());
    EXPECT_EQ(actual,expected);
    EXPECT_EQ(std::adjacent_find(actual.begin(),actual.end()),actual.end());
    return actual;
}
}
TEST(Clipper2NextCatalogPairTests, EmptyAndSingletonAreCompleteWithoutVisitorCalls) {
    EXPECT_TRUE(collect({}).empty());
    EXPECT_TRUE(collect({{0,0,0,0}}).empty());
}
TEST(Clipper2NextCatalogPairTests, ClosedPointEdgeAndCornerContactsAreIncluded) {
    EXPECT_FALSE(collect({{0,0,10,10},{10,10,10,10},{10,0,20,10},{11,11,12,12},{3,3,3,3}}).empty());
}
TEST(Clipper2NextCatalogPairTests, NonPowerOfTwoAndOrdinalPermutationKeepExactPairs) {
    std::vector<next::Rect64> boxes{{0,0,3,3},{2,2,5,5},{4,4,8,8},{20,0,30,1},{3,-2,4,4},{-1,-1,1,1},{30,1,31,2}};
    collect(boxes);std::reverse(boxes.begin(),boxes.end());collect(boxes);
}
TEST(Clipper2NextCatalogPairTests, AllAdmittedCoordinatesAndLargeTranslationStayExact) {
    constexpr auto m=next::MAX_COORD;
    collect({{-m,-m,m,m},{-m,0,-m,m},{m,m,m,m},{-1,-1,1,1},{m-1,-m,m,0}});
    constexpr auto t=int64_t{1}<<59;
    collect({{t,t,t+10,t+10},{t+10,t,t+20,t+10},{t+11,t+11,t+12,t+12}});
}
TEST(Clipper2NextCatalogPairTests, HighCardinalityHasNoDuplicateOrTruncatedPairs) {
    EXPECT_EQ(collect(std::vector<next::Rect64>(65,{0,0,10,10})).size(),65U*64U/2U);
}
TEST(Clipper2NextCatalogPairTests, FalseVisitorStopsAtFirstActualPair) {
    in::triangulation_bounds_catalog catalog;
    catalog.build(9,[](std::size_t){return next::Rect64{0,0,1,1};});
    std::size_t calls=0;
    EXPECT_FALSE(catalog.query_pairs([&](std::size_t,std::size_t){++calls;return false;}));
    EXPECT_EQ(calls,1U);
}

TEST(Clipper2NextCatalogPairTests, CoincidentLayoutKeysKeepDistinctWideRangeRecords) {
    constexpr auto m=next::MAX_COORD;
    std::vector<next::Rect64> boxes{{-m,-m,m,m}};
    for (int64_t i=0;i<257;++i) {
        boxes.push_back({i-128,0,i-127,1});
    }
    boxes.push_back({0,0,0,0});
    boxes.push_back({0,0,0,0});
    collect(boxes);
    std::reverse(boxes.begin(),boxes.end());
    collect(boxes);
}

TEST(Clipper2NextCatalogPairTests, ZeroExtentAndAnisotropicDirectoriesKeepClosedPairs) {
    std::vector<next::Rect64> boxes;
    for (int64_t i=0;i<129;++i) { boxes.push_back({0,i,0,i+1}); }
    collect(boxes);
    for (auto& b:boxes) { b={b.top,0,b.bottom,0}; }
    collect(boxes);
    collect(std::vector<next::Rect64>(129,{0,0,0,0}));
}

TEST(Clipper2NextCatalogPairTests, RandomizedWideTranslationsAndPermutationsAgreeWithCompletePairs) {
    std::mt19937_64 generator{431927};
    for (int trial=0;trial<24;++trial) {
        const int64_t shift=trial%3==0 ? -(int64_t{1}<<60) : trial%3==1 ? int64_t{1}<<60 : 0;
        std::vector<next::Rect64> boxes;
        for (int i=0;i<65+trial;++i) {
            const auto x=static_cast<int64_t>(generator()%500)-250;
            const auto y=static_cast<int64_t>(generator()%500)-250;
            boxes.push_back({shift+x,shift+y,shift+x+static_cast<int64_t>(generator()%70),
                             shift+y+static_cast<int64_t>(generator()%70)});
        }
        collect(boxes);
        std::shuffle(boxes.begin(),boxes.end(),generator);
        collect(boxes);
    }
}
