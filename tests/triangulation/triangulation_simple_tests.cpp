#include <gtest/gtest.h>

#include "triangulation/private/triangulation_simple.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_topology.h"
#include "triangulation/private/triangulation_result_builder.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_executor.h"
#include "triangulation/private/triangulation_operation.h"
#include "geometry/private/geometry_predicates.h"

#include <algorithm>

namespace next = clipper2next;
namespace in = next::internal;

namespace {
auto verify_graph(in::triangulation_context& context, std::size_t count,
                  int64_t expected_area2) -> void {
    ASSERT_EQ(context.vertices.size(), count);
    ASSERT_EQ(context.triangles.size(), count - 2);
    std::size_t boundary = 0;
    for (auto edge : context.edges) {
        ASSERT_NE(edge->triA, nullptr);
        if (in::is_loose_edge(*edge)) { ASSERT_NE(edge->triB, nullptr); }
        else { ++boundary; EXPECT_EQ(edge->triB, nullptr); }
    }
    EXPECT_EQ(boundary, count);
    context.use_delaunay = false;
    ASSERT_TRUE(in::legalize_pending_delaunay_edges(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)));
    const auto triangles = in::build_triangulation_result(in::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)));
    int64_t total = 0;
    for (const auto& triangle : triangles) {
        ASSERT_EQ(triangle.size(), 3U);
        const auto a = triangle[0], b = triangle[1], c = triangle[2];
        const auto value = (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        ASSERT_GT(value, 0);
        total += value;
    }
    EXPECT_EQ(total, expected_area2);
}
}  // namespace

TEST(Clipper2NextTriangulationSimpleTests, SweepMaterializesExactFaceOrderWithoutMutableGraph) {
    const next::Path64 original{{0, 0}, {10, 0}, {10, 5}, {7, 6}, {10, 10}, {0, 10}, {2, 5}};
    for (std::size_t start = 0; start < original.size(); ++start) {
        for (const bool reverse : {false, true}) {
            auto path = original;
            std::rotate(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(start), path.end());
            if (reverse) { std::reverse(path.begin(), path.end()); }
            in::triangulation_operation_lease reference_operation;
            auto reference_status = next::TriangulateResult::fail;
            const auto mesh = in::execute_triangulation_graph(reference_operation, {path}, false, reference_status);
            ASSERT_EQ(reference_status, next::TriangulateResult::success);
            const auto reference = in::build_triangulation_result(mesh);
            in::triangulation_operation_lease operation;
            auto status = next::TriangulateResult::fail;
            next::Paths64 output{{{123, 456}}};
            in::execute_sweep_triangulation_into(operation, {path}, status, output);
            EXPECT_EQ(status, reference_status);
            EXPECT_EQ(output, reference);
            EXPECT_TRUE(operation.context().vertices.empty());
            EXPECT_TRUE(operation.context().edges.empty());
            EXPECT_TRUE(operation.context().triangles.empty());
            EXPECT_EQ(operation.context().vertex_pool.retained_capacity(), 0U);
            EXPECT_EQ(operation.context().edge_pool.retained_capacity(), 0U);
            EXPECT_EQ(operation.context().triangle_pool.retained_capacity(), 0U);
            next::PathsD expected_scaled;
            in::build_triangulation_result_into(mesh, expected_scaled, 0.01);
            in::triangulation_operation_lease scaled_operation;
            next::PathsD scaled;
            in::execute_sweep_triangulation_into(scaled_operation, {path}, status, scaled, 0.01);
            EXPECT_EQ(scaled, expected_scaled);
            EXPECT_TRUE(scaled_operation.context().edges.empty());
        }
    }
}

TEST(Clipper2NextTriangulationSimpleTests, SweepUsesGeneralBoundaryForUncertifiedAndMultipleRings) {
    const std::vector<next::Paths64> inputs{
        {{{0,0},{10,0},{10,10},{7,10},{7,3},{3,3},{3,10},{0,10}}},
        {{{0,0},{10,0},{10,10},{0,10}},{{2,2},{2,8},{8,8},{8,2}}},
        {},
    };
    for (const auto& paths : inputs) {
        in::triangulation_operation_lease reference_operation;
        auto expected_status = next::TriangulateResult::fail;
        const auto expected = in::build_triangulation_result(
            in::execute_triangulation_graph(reference_operation, paths, false, expected_status));
        in::triangulation_operation_lease operation;
        auto status = next::TriangulateResult::fail;
        next::Paths64 output{{{123,456}}};
        in::execute_sweep_triangulation_into(operation, paths, status, output);
        EXPECT_EQ(status, expected_status);
        EXPECT_EQ(output, expected);
    }
}

TEST(Clipper2NextTriangulationSimpleTests, MonotoneConcavitiesSurviveEveryStartAndReversal) {
    const next::Path64 original{{0, 0}, {10, 0}, {10, 5}, {7, 6}, {10, 10}, {0, 10}, {2, 5}};
    for (std::size_t start = 0; start != original.size(); ++start) {
        for (bool reverse : {false, true}) {
            auto path = original;
            std::rotate(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(start), path.end());
            if (reverse) { std::reverse(path.begin(), path.end()); }
            const auto before = path;
            in::triangulation_context context;
            ASSERT_TRUE(in::try_build_simple_triangulation_graph(context, {path}));
            EXPECT_EQ(path, before);
            verify_graph(context, path.size(), 165);
        }
    }
}

TEST(Clipper2NextTriangulationSimpleTests, HorizontalVerticalAndZeroTurnsKeepAllConstraints) {
    const next::Path64 path{{0,0},{2,0},{5,0},{10,0},{10,3},{10,7},
                            {10,10},{7,10},{3,10},{0,10},{0,7},{0,3}};
    in::triangulation_context context;
    ASSERT_TRUE(in::try_build_simple_triangulation_graph(context, {path}));
    verify_graph(context, path.size(), 200);
}

TEST(Clipper2NextTriangulationSimpleTests, CrossingTouchAndNonMonotoneRingsLeaveNoPartialGraph) {
    const next::Paths64 rings{
        {{0,50},{29,-40},{-48,15},{48,15},{-29,-40}},
        {{0,0},{10,10},{0,10},{10,0}},
        {{0,0},{10,0},{10,10},{5,5},{0,10},{5,5}},
        {{0,0},{10,0},{10,10},{7,10},{7,3},{3,3},{3,10},{0,10}},
        {{0,0},{10,0},{10,10},{0,10},{0,0}},
    };
    for (const auto& ring : rings) {
        in::triangulation_context context;
        EXPECT_FALSE(in::try_build_simple_triangulation_graph(context, {ring}));
        EXPECT_TRUE(context.vertices.empty());
        EXPECT_TRUE(context.edges.empty());
        EXPECT_TRUE(context.triangles.empty());
    }
}

TEST(Clipper2NextTriangulationSimpleTests, BothChainsMustRemainSeparatedAtEveryEvent) {
    const next::Path64 crossing{{0,0},{8,2},{2,6},{8,10},{0,10},{6,7},{0,3}};
    in::triangulation_context context;
    EXPECT_FALSE(in::try_build_simple_triangulation_graph(context, {crossing}));
    EXPECT_TRUE(context.vertices.empty());
    EXPECT_TRUE(context.edges.empty());
}

TEST(Clipper2NextTriangulationSimpleTests, MultipleRingsStayWithTheGeneralBoundaryOwner) {
    in::triangulation_context context;
    const next::Paths64 paths{{{0,0},{10,0},{10,10},{0,10}},
                              {{2,2},{2,8},{8,8},{8,2}}};
    EXPECT_FALSE(in::try_build_simple_triangulation_graph(context, paths));
    EXPECT_TRUE(context.vertices.empty());
    EXPECT_TRUE(context.edges.empty());
}

TEST(Clipper2NextTriangulationSimpleTests, ExactProduct96KeepsFourRealConcavitiesAndZeroTurns) {
    const next::Path64 path{{1420,1000},{1419,1019},{1416,1038},{1412,1057},{1406,1075},{1398,1093},{1388,1111},{1377,1128},{1364,1145},{1349,1161},{1333,1177},{1316,1191},{1297,1205},{1277,1218},{1256,1230},{1233,1241},{1210,1251},{1186,1260},{1161,1268},{1135,1275},{1109,1280},{1082,1284},{1055,1288},{1027,1289},{1000,1290},{973,1289},{945,1288},{918,1284},{891,1280},{865,1275},{839,1268},{814,1260},{790,1251},{767,1241},{744,1230},{723,1218},{703,1205},{684,1191},{667,1177},{651,1161},{636,1145},{623,1128},{612,1111},{602,1093},{594,1075},{588,1057},{584,1038},{581,1019},{580,1000},{581,981},{584,962},{588,943},{594,925},{602,907},{612,889},{623,872},{636,855},{651,839},{667,823},{684,809},{703,795},{723,782},{744,770},{767,759},{790,749},{814,740},{839,732},{865,725},{891,720},{918,716},{945,712},{973,711},{1000,710},{1027,711},{1055,712},{1082,716},{1109,720},{1135,725},{1161,732},{1186,740},{1210,749},{1233,759},{1256,770},{1277,782},{1297,795},{1316,809},{1333,823},{1349,839},{1364,855},{1377,872},{1388,889},{1398,907},{1406,925},{1412,943},{1416,962},{1419,981}};
    std::size_t zero = 0, negative = 0;
    for (std::size_t i = 0; i != path.size(); ++i) {
        const auto a = path[i], b = path[(i+1)%path.size()], c = path[(i+2)%path.size()];
        const auto turn = (b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        zero += turn == 0;
        negative += turn < 0;
    }
    ASSERT_EQ(zero, 4U);
    ASSERT_EQ(negative, 4U);
    in::triangulation_context context;
    ASSERT_TRUE(in::try_build_simple_triangulation_graph(context, {path}));
    verify_graph(context, 96, 764768);
}
