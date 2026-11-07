#include <gtest/gtest.h>
#include "triangulation/private/triangulation_catalog.h"
#include <algorithm>
#include <array>

namespace next=clipper2next;
namespace in=next::internal;
namespace {
auto query(const in::triangulation_bounds_catalog& catalog,next::Point64 a,next::Point64 b) {
    std::vector<std::size_t> ids;
    EXPECT_TRUE(catalog.query_segment(a,b,[&](std::size_t id){ids.push_back(id);return true;}));
    std::sort(ids.begin(),ids.end());return ids;
}
}
TEST(Clipper2NextTriangulationSegmentQueryTests, DiagonalQueryRejectsBothStrictHalfPlanes) {
    const std::array<next::Point64,7> points{{{0,0},{5,5},{10,10},{0,10},{10,0},{3,8},{11,11}}};
    in::triangulation_bounds_catalog catalog;
    catalog.build(points.size(),[&](std::size_t id){return in::triangulation_bounds(points[id],points[id]);});
    EXPECT_EQ(query(catalog,{0,0},{10,10}),(std::vector<std::size_t>{0,1,2}));
    EXPECT_EQ(query(catalog,{10,10},{0,0}),(std::vector<std::size_t>{0,1,2}));
}
TEST(Clipper2NextTriangulationSegmentQueryTests, ClosedCornerAndEdgeContactsRemainCandidates) {
    const std::array<next::Rect64,6> boxes{{{5,5,8,9},{1,1,4,4},{2,5,3,6},{5,2,6,3},{10,10,12,12},{11,11,12,12}}};
    in::triangulation_bounds_catalog catalog;
    catalog.build(boxes.size(),[&](std::size_t id){return boxes[id];});
    EXPECT_EQ(query(catalog,{0,0},{10,10}),(std::vector<std::size_t>{0,1,4}));
}
TEST(Clipper2NextTriangulationSegmentQueryTests, HorizontalAndVerticalBoundsHaveNoOpenIntervalGap) {
    const std::array<next::Rect64,6> boxes{{{5,0,5,10},{0,5,10,5},{5,5,5,5},{6,6,7,7},{5,11,5,12},{-2,5,-1,5}}};
    in::triangulation_bounds_catalog catalog;
    catalog.build(boxes.size(),[&](std::size_t id){return boxes[id];});
    EXPECT_EQ(query(catalog,{5,0},{5,10}),(std::vector<std::size_t>{0,1,2}));
    EXPECT_EQ(query(catalog,{0,5},{10,5}),(std::vector<std::size_t>{0,1,2}));
}
TEST(Clipper2NextTriangulationSegmentQueryTests, DegeneratePointQueryUsesTheSameClosedOwnership) {
    const std::array<next::Rect64,5> boxes{{{0,0,10,10},{5,5,5,5},{0,0,4,4},{6,6,10,10},{5,0,5,10}}};
    in::triangulation_bounds_catalog catalog;
    catalog.build(boxes.size(),[&](std::size_t id){return boxes[id];});
    EXPECT_EQ(query(catalog,{5,5},{5,5}),(std::vector<std::size_t>{0,1,4}));
}
TEST(Clipper2NextTriangulationSegmentQueryTests, FullAdmittedDiagonalUsesExactSideExtrema) {
    constexpr auto m=next::MAX_COORD;
    const std::array<next::Point64,6> points{{{-m,-m},{0,0},{m,m},{0,1},{1,0},{-m,m}}};
    in::triangulation_bounds_catalog catalog;
    catalog.build(points.size(),[&](std::size_t id){return in::triangulation_bounds(points[id],points[id]);});
    EXPECT_EQ(query(catalog,{-m,-m},{m,m}),(std::vector<std::size_t>{0,1,2}));
}
TEST(Clipper2NextTriangulationSegmentQueryTests, EarlyStopClearReleaseAndRebuildKeepCurrentOrdinals) {
    in::triangulation_bounds_catalog catalog;
    catalog.build(20,[](std::size_t id){const auto n=static_cast<int64_t>(id);return next::Rect64{n,n,n,n};});
    std::size_t visits=0;
    EXPECT_FALSE(catalog.query_segment({0,0},{19,19},[&](std::size_t){++visits;return false;}));
    EXPECT_EQ(visits,1U);catalog.clear();EXPECT_TRUE(query(catalog,{0,0},{19,19}).empty());
    catalog.build(1,[](std::size_t){return next::Rect64{100,100,100,100};});
    EXPECT_EQ(query(catalog,{99,99},{101,101}),(std::vector<std::size_t>{0}));
    catalog.release();EXPECT_TRUE(query(catalog,{99,99},{101,101}).empty());
}
TEST(Clipper2NextTriangulationSegmentQueryTests, RootRangeControlsTheExactArithmeticDomain) {
    constexpr int64_t n=(int64_t{1}<<31)-1;
    constexpr auto m=next::MAX_COORD;
    const std::array<next::Point64,6> points{{{0,0},{n,n},{m,-m},{-m,m},{1,1},{0,2}}};
    in::triangulation_bounds_catalog catalog;
    catalog.build(points.size(),[&](std::size_t id){return in::triangulation_bounds(points[id],points[id]);});
    EXPECT_EQ(query(catalog,{0,0},{n,n}),(std::vector<std::size_t>{0,1,4}));
}
TEST(Clipper2NextTriangulationSegmentQueryTests, Entire31BitRootKeepsDiagonalPointsAtExtremeSigns) {
    constexpr int64_t n=(int64_t{1}<<31)-1;
    const std::array<next::Point64,6> points{{{-n,-n},{0,0},{n,n},{-n,n},{n,-n},{2,3}}};
    in::triangulation_bounds_catalog catalog;
    catalog.build(points.size(),[&](std::size_t id){return in::triangulation_bounds(points[id],points[id]);});
    EXPECT_EQ(query(catalog,{0,0},{n,n}),(std::vector<std::size_t>{1,2}));
    EXPECT_EQ(query(catalog,{n,n},{0,0}),(std::vector<std::size_t>{1,2}));
}
