#include <gtest/gtest.h>
#include "triangulation/private/triangulation_catalog.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "triangulation/private/triangulation_topology.h"
#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include <algorithm>
#include <array>
#include <vector>

namespace next = clipper2next;
namespace in = clipper2next::internal;

namespace {
auto query(const in::triangulation_bounds_catalog& catalog, next::Rect64 bounds)
    -> std::vector<std::size_t> {
    std::vector<std::size_t> result;
    EXPECT_TRUE(catalog.query(bounds, [&](std::size_t id) { result.push_back(id); return true; }));
    std::sort(result.begin(), result.end());
    return result;
}
}  // namespace

TEST(Clipper2NextTriangulationCatalogTests, ClosedContactsKeepEveryDistinctEntry) {
    const std::array<next::Rect64, 7> boxes{{
        {0, 0, 10, 10}, {10, 10, 10, 10}, {10, -5, 10, 5},
        {-5, 10, 5, 10}, {11, 11, 12, 12}, {10, 10, 10, 10}, {-1, -1, -1, -1},
    }};
    in::triangulation_bounds_catalog catalog;
    catalog.build(boxes.size(), [&](std::size_t i) { return boxes[i]; });
    EXPECT_EQ(query(catalog, {10, 10, 10, 10}), (std::vector<std::size_t>{0, 1, 5}));
    EXPECT_EQ(query(catalog, {10, 0, 10, 10}), (std::vector<std::size_t>{0, 1, 2, 5}));
    EXPECT_EQ(query(catalog, {-1, -1, 0, 0}), (std::vector<std::size_t>{0, 6}));
    EXPECT_TRUE(query(catalog, {50, 50, 50, 50}).empty());
}

TEST(Clipper2NextTriangulationCatalogTests, FullAdmittedRangeHasNoRoundedPartitionBoundary) {
    constexpr auto m = next::MAX_COORD;
    const std::array<next::Rect64, 6> boxes{{
        {-m, -m, m, m}, {-m, 0, -m, 0}, {m, 0, m, 0},
        {-1, 0, -1, 0}, {0, 0, 0, 0}, {1, 0, 1, 0},
    }};
    in::triangulation_bounds_catalog catalog;
    catalog.build(boxes.size(), [&](std::size_t i) { return boxes[i]; });
    EXPECT_EQ(query(catalog, {-m, 0, -m, 0}), (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(query(catalog, {m, 0, m, 0}), (std::vector<std::size_t>{0, 2}));
    EXPECT_EQ(query(catalog, {-1, 0, 1, 0}), (std::vector<std::size_t>{0, 3, 4, 5}));
}

TEST(Clipper2NextTriangulationCatalogTests, ClearReleaseAndRebuildNeverReuseOldEntries) {
    in::triangulation_bounds_catalog catalog;
    catalog.build(17, [](std::size_t i) {
        const auto x = static_cast<int64_t>(i);
        return next::Rect64{x, 0, x, 0};
    });
    EXPECT_EQ(query(catalog, {8, 0, 8, 0}), (std::vector<std::size_t>{8}));
    std::size_t visits = 0;
    EXPECT_FALSE(catalog.query({0, 0, 16, 0}, [&](std::size_t) { ++visits; return false; }));
    EXPECT_EQ(visits, 1U);
    catalog.clear();
    EXPECT_TRUE(query(catalog, {8, 0, 8, 0}).empty());
    catalog.build(1, [](std::size_t) { return next::Rect64{40, 0, 40, 0}; });
    EXPECT_TRUE(query(catalog, {8, 0, 8, 0}).empty());
    EXPECT_EQ(query(catalog, {40, 0, 40, 0}), (std::vector<std::size_t>{0}));
    catalog.release();
    EXPECT_TRUE(query(catalog, {40, 0, 40, 0}).empty());
}

TEST(Clipper2NextTriangulationCatalogTests, VisibilityKeepsActiveEndpointIdentityAfterSweepReordering) {
    in::triangulation_context context;
    auto* a = in::create_triangulation_vertex(context, {0, 0});
    auto* b = in::create_triangulation_vertex(context, {10, 10});
    auto* middle = in::create_triangulation_vertex(context, {5, 5});
    auto* constraint = in::create_triangulation_edge(context, middle, b, in::triangulation_edge_kind::ascend);
    in::add_sweep_active_edge(context, constraint);
    (void)in::create_triangulation_vertex(context, {20, 0});
    std::rotate(context.vertices.begin(), context.vertices.begin() + 3, context.vertices.end());
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, a, b));
}

TEST(Clipper2NextTriangulationCatalogTests, ActiveBoundaryConstraintBlocksVisibility) {
    in::triangulation_context context;
    auto* a = in::create_triangulation_vertex(context, {0, 0});
    auto* b = in::create_triangulation_vertex(context, {10, 10});
    auto* c = in::create_triangulation_vertex(context, {0, 10});
    auto* d = in::create_triangulation_vertex(context, {10, 0});
    auto* constraint = in::create_triangulation_edge(context, c, d, in::triangulation_edge_kind::ascend);
    in::add_sweep_active_edge(context, constraint);
    ASSERT_TRUE(constraint->isActive);
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, a, b));
}

TEST(Clipper2NextTriangulationCatalogTests, LooseVisibilityUsesActualActivityLifecycle) {
    in::triangulation_context context;
    auto* a = in::create_triangulation_vertex(context, {0, 0});
    auto* b = in::create_triangulation_vertex(context, {10, 10});
    auto* c = in::create_triangulation_vertex(context, {0, 10});
    auto* d = in::create_triangulation_vertex(context, {10, 0});
    auto* loose = in::create_triangulation_edge(context, c, d);
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, a, b));
    in::add_sweep_active_edge(context, loose);
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, a, b));
    in::remove_sweep_active_edge(context, loose);
    ASSERT_FALSE(context.internal_error);
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, a, b));
}

TEST(Clipper2NextTriangulationCatalogTests, ContextResetDropsOldActiveGeometryBeforeRebuild) {
    in::triangulation_context context;
    auto* a = in::create_triangulation_vertex(context, {0, 0});
    auto* b = in::create_triangulation_vertex(context, {10, 10});
    auto* middle = in::create_triangulation_vertex(context, {5, 5});
    auto* constraint = in::create_triangulation_edge(context, middle, b, in::triangulation_edge_kind::ascend);
    in::add_sweep_active_edge(context, constraint);
    ASSERT_FALSE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, a, b));
    context.clear();
    a = in::create_triangulation_vertex(context, {0, 0});
    b = in::create_triangulation_vertex(context, {10, 10});
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, a, b));
    context.release();
}

TEST(Clipper2NextTriangulationContourQueryTests, NonContactNestedRingsKeepRoleAcrossOrderAndReversal) {
    const next::Paths64 original{{{0,0},{100,0},{100,100},{0,100}},
        {{20,20},{20,80},{80,80},{80,20}},{{40,40},{60,40},{60,60},{40,60}}};
    for(bool reverse:{false,true}) for(std::size_t rotate=0;rotate<3;++rotate) {
        auto paths=original;std::rotate(paths.begin(),paths.begin()+rotate,paths.end());
        if(reverse) for(auto& path:paths) { std::reverse(path.begin(),path.end()); }
        in::triangulation_context context;
        ASSERT_EQ(in::build_triangulation_boundary(context,paths),next::TriangulateResult::success);
        EXPECT_EQ(context.boundary_events.size(),12U);
        for(const auto& probe:std::array<std::pair<next::Point64,int64_t>,4>{{{{10,10},1},{{30,30},0},{{50,50},1},{{110,110},0}}}) {
            int64_t winding=0;
            for(std::size_t ring=0;ring!=context.source_paths.size();++ring) { winding+=in::boundary_winding(context.source_paths[ring],probe.first,probe.first,{0,0}); }
            EXPECT_EQ(winding,probe.second);
        }
    }
}
TEST(Clipper2NextTriangulationContourQueryTests, ClosedEndpointContactRetainsSeparateOccupiedSectors) {
    in::triangulation_context context;
    const next::Paths64 paths{{{0,0},{20,0},{20,20},{0,20}},{{0,10},{4,14},{4,6}}};
    ASSERT_EQ(in::build_triangulation_boundary(context,paths),next::TriangulateResult::success);
    const auto found=std::find_if(context.vertices.begin(),context.vertices.end(),[](auto v){return v->pt==next::Point64{0,10};});
    ASSERT_NE(found,context.vertices.end());EXPECT_EQ((*found)->event_end-(*found)->event_begin,2U);
}
TEST(Clipper2NextTriangulationContourQueryTests, ProperCrossingIsNeverClassifiedAsAConstantRelation) {
    in::triangulation_context context;
    const next::Paths64 paths{{{0,0},{20,0},{20,20},{0,20}},{{10,-5},{10,25},{25,25},{25,-5}}};
    EXPECT_EQ(in::build_triangulation_boundary(context,paths),next::TriangulateResult::paths_intersect);
    EXPECT_TRUE(context.boundary_events.empty());
}
TEST(Clipper2NextTriangulationContourQueryTests, SameDirectionNestedRingsStillRejectBeforeMaterialization) {
    in::triangulation_context context;
    const next::Paths64 paths{{{0,0},{20,0},{20,20},{0,20}},{{5,5},{15,5},{15,15},{5,15}}};
    EXPECT_THROW((void)in::build_triangulation_boundary(context,paths),next::clipper_error);
    EXPECT_TRUE(context.boundary_events.empty());EXPECT_TRUE(context.vertices.empty());
}
TEST(Clipper2NextTriangulationContourQueryTests, ConcaveNonContactBoundaryHasAnExactConstantExteriorRelation) {
    in::triangulation_context context;
    const next::Paths64 paths{{{0,0},{20,0},{20,5},{5,5},{5,20},{0,20}},
        {{8,8},{16,8},{16,16},{8,16}}};
    ASSERT_EQ(in::build_triangulation_boundary(context,paths),next::TriangulateResult::success);
    EXPECT_EQ(context.boundary_events.size(),10U);
}

TEST(Clipper2NextTriangulationContourRelationControlTests, ContactRelationsAgreeBeforeGraphSlotsAreCreated) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{{{0,0},{20,0},{20,20},{0,20}},
                              {{10,0},{10,20},{30,20},{30,0}}};
    EXPECT_THROW((void)next::internal::build_triangulation_boundary(context,paths),next::clipper_error);
    EXPECT_TRUE(context.vertices.empty());
    EXPECT_TRUE(context.edges.empty());
    EXPECT_TRUE(context.boundary_events.empty());
}
