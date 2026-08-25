#include <gtest/gtest.h>

#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_sweep_line.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationSweepLineTests, ActiveEdgeInsertionIsIdempotent) {
    next::internal::triangulation_context context;
    auto* first = next::internal::create_triangulation_vertex(context, {0, 10});
    auto* second = next::internal::create_triangulation_vertex(context, {10, 0});
    auto* edge = next::internal::create_triangulation_edge(
        context, first, second, next::internal::triangulation_edge_kind::ascend);

    next::internal::add_sweep_active_edge(context, edge);
    next::internal::add_sweep_active_edge(context, edge);

    EXPECT_TRUE(edge->isActive);
    EXPECT_EQ(context.first_active, edge);
    EXPECT_EQ(edge->nextE, nullptr);
}

TEST(Clipper2NextTriangulationSweepLineTests, ActiveEdgeRemovalRelinksNeighborEdges) {
    next::internal::triangulation_context context;
    auto* a = next::internal::create_triangulation_vertex(context, {0, 10});
    auto* b = next::internal::create_triangulation_vertex(context, {10, 0});
    auto* c = next::internal::create_triangulation_vertex(context, {20, 10});
    auto* d = next::internal::create_triangulation_vertex(context, {30, 0});
    auto* first = next::internal::create_triangulation_edge(context, a, b);
    auto* second = next::internal::create_triangulation_edge(context, c, d);

    next::internal::add_sweep_active_edge(context, first);
    next::internal::add_sweep_active_edge(context, second);
    next::internal::remove_sweep_active_edge(context, second);

    EXPECT_FALSE(second->isActive);
    EXPECT_EQ(context.first_active, first);
    EXPECT_EQ(first->prevE, nullptr);
}

TEST(Clipper2NextTriangulationSweepLineTests, SweepTriangulatesHorizontalSquare) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {40, 0}, {40, 40}, {0, 40}},
    };

    ASSERT_TRUE(next::internal::build_triangulation_boundary(context, paths));

    EXPECT_EQ(next::internal::run_triangulation_sweep(context), next::TriangulateResult::success);
    EXPECT_FALSE(context.triangles.empty());
}

TEST(Clipper2NextTriangulationSweepLineTests, SweepHandlesDuplicateXVerticalBounds) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {0, 20}, {10, 30}, {20, 20}, {20, 0}},
    };

    ASSERT_TRUE(next::internal::build_triangulation_boundary(context, paths));

    EXPECT_EQ(next::internal::run_triangulation_sweep(context), next::TriangulateResult::success);
    EXPECT_FALSE(context.triangles.empty());
}
