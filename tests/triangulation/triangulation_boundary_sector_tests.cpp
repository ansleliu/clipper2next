#include "triangulation/private/triangulation_boundary_sector.h"
#include "triangulation/private/triangulation_boundary.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <optional>

namespace next=clipper2next;
namespace in=next::internal;

namespace {
auto event(const in::triangulation_context& context,next::Point64 point)
    ->const in::boundary_corner_event& {
    const auto found=std::find_if(context.boundary_events.begin(),context.boundary_events.end(),
        [&](const auto& value){return value.vertex->pt==point;});
    if(found==context.boundary_events.end()){throw std::runtime_error("missing fixture event");}
    return *found;
}
}

TEST(Clipper2NextTriangulationBoundarySectorTests, ConvexCornerKeepsStrictAndClosedBoundaryModes) {
    in::triangulation_context context;
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{0,0},{10,0},{10,10},{0,10}}}),next::TriangulateResult::success);
    const auto sector=event(context,{0,0}).sector;
    EXPECT_TRUE(sector.contains_ray({1,1},false));
    EXPECT_FALSE(sector.contains_ray({-1,1},true));
    EXPECT_TRUE(sector.contains_ray({1,0},true));
    EXPECT_FALSE(sector.contains_ray({1,0},false));
    EXPECT_TRUE(sector.contains_triangle({10,0},{0,10}));
    EXPECT_TRUE(sector.contains_triangle({0,10},{10,0}));
    EXPECT_FALSE(sector.contains_triangle({5,5},{5,-5}));
}

TEST(Clipper2NextTriangulationBoundarySectorTests, ReflexSourceRaysDoNotFillTheHole) {
    in::triangulation_context context;
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{0,0},{10,0},{10,10},{0,10}},
        {{2,2},{2,8},{8,8},{8,2}}}),next::TriangulateResult::success);
    const auto sector=event(context,{2,2}).sector;
    EXPECT_FALSE(sector.contains_ray({1,1},false));
    EXPECT_TRUE(sector.contains_ray({-1,-1},false));
    EXPECT_FALSE(sector.contains_triangle({2,8},{8,2}));
    EXPECT_FALSE(sector.contains_triangle({8,2},{2,8}));
    EXPECT_TRUE(sector.contains_triangle({2,8},{0,2}));
    EXPECT_TRUE(sector.contains_triangle({8,2},{2,0}));
}

TEST(Clipper2NextTriangulationBoundarySectorTests, OppositeRaysKeepTheExactHalfPlane) {
    in::triangulation_context context;
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{-10,0},{0,0},{10,0},{10,10},{-10,10}}}),next::TriangulateResult::success);
    const auto sector=event(context,{0,0}).sector;
    EXPECT_TRUE(sector.contains_ray({0,1},false));
    EXPECT_FALSE(sector.contains_ray({0,-1},true));
    EXPECT_FALSE(sector.contains_triangle({-10,0},{10,0}));
    EXPECT_TRUE(sector.contains_triangle({-10,0},{0,5}));
}

TEST(Clipper2NextTriangulationBoundarySectorTests, FullAdmittedRangeHasNoAngleRounding) {
    constexpr auto m=next::MAX_COORD;
    in::triangulation_context context;
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{-m,-m},{m,-m},{m,m},{-m,m}}}),next::TriangulateResult::success);
    const auto sector=event(context,{-m,-m}).sector;
    EXPECT_TRUE(sector.contains_triangle({m,-m},{-m,m}));
    EXPECT_TRUE(sector.contains_triangle({-m,m},{m,-m}));
    EXPECT_TRUE(sector.contains_ray({1,1},false));
    EXPECT_FALSE(sector.contains_ray({-1,-1},true));
}

TEST(Clipper2NextTriangulationBoundarySectorTests, NumericSnapshotSurvivesSourceDestruction) {
    auto sector=[]{
        in::triangulation_context context;
        if(in::build_triangulation_boundary(context,{{{0,0},{10,0},{10,10},{0,10}}})!=next::TriangulateResult::success){throw std::runtime_error("fixture boundary failed");}
        return event(context,{0,0}).sector;
    }();
    EXPECT_TRUE(sector.contains_ray({1,1},false));
    EXPECT_TRUE(sector.contains_triangle({10,0},{0,10}));
    EXPECT_FALSE(sector.contains_ray({-1,1},true));
}

TEST(Clipper2NextTriangulationBoundarySectorTests, EventSnapshotSurvivesVectorGrowthAndGraphRelease) {
    in::triangulation_context context;
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{0,0},{10,0},{10,10},{0,10}}}),next::TriangulateResult::success);
    const auto snapshot=event(context,{0,0}).sector;
    context.boundary_events.reserve(context.boundary_events.capacity()+128U);
    const auto& moved=event(context,{0,0}).sector;
    EXPECT_TRUE(moved.contains_ray({1,1},false));
    EXPECT_FALSE(moved.contains_ray({1,0},false));
    EXPECT_TRUE(moved.contains_triangle({10,0},{0,10}));
    context.release();
    EXPECT_TRUE(snapshot.contains_ray({1,1},false));
    EXPECT_FALSE(snapshot.contains_ray({-1,1},true));
    EXPECT_TRUE(snapshot.contains_triangle({10,0},{0,10}));
}
TEST(Clipper2NextTriangulationBoundarySectorTests, DirectNumericReflexSnapshotMatchesAdmittedEvent) {
    const in::boundary_sector direct{{2,2},{2,8},{8,2}};
    in::triangulation_context context;
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{0,0},{10,0},{10,10},{0,10}},
        {{2,2},{2,8},{8,8},{8,2}}}),next::TriangulateResult::success);
    const auto& owned=event(context,{2,2}).sector;
    for(in::boundary_ray ray: {in::boundary_ray{1,1},in::boundary_ray{-1,-1},in::boundary_ray{0,1},in::boundary_ray{1,0}})
    for(bool closed:{false,true})EXPECT_EQ(owned.contains_ray(ray,closed),direct.contains_ray(ray,closed));
    EXPECT_FALSE(direct.contains_triangle({2,8},{8,2}));
    EXPECT_TRUE(direct.contains_triangle({2,8},{0,2}));
}
