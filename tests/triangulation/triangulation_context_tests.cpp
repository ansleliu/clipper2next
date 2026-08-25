#include <gtest/gtest.h>

#include "triangulation/private/triangulation_context.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationContextTests, OwnsStablePointersAndClearsViews) {
    next::internal::triangulation_context context;
    auto* first = next::internal::create_triangulation_vertex(context, {0, 0});
    for (int index = 1; index < 64; ++index) {
        (void)next::internal::create_triangulation_vertex(context, {index, index});
    }
    auto* second = context.vertices[1].get();
    auto* edge = next::internal::create_triangulation_edge(
        context, first, second, next::internal::triangulation_edge_kind::ascend);
    auto* triangle = next::internal::create_triangulation_triangle(context, edge, edge, edge);

    EXPECT_EQ(first->pt, next::Point64(0, 0));
    EXPECT_EQ(context.vertices.front(), first);
    EXPECT_EQ(context.edges.front(), edge);
    EXPECT_EQ(context.triangles.front(), triangle);

    context.clear();

    EXPECT_TRUE(context.vertex_pool.empty());
    EXPECT_TRUE(context.edge_pool.empty());
    EXPECT_TRUE(context.triangle_pool.empty());
    EXPECT_TRUE(context.vertices.empty());
    EXPECT_TRUE(context.edges.empty());
    EXPECT_TRUE(context.triangles.empty());
}

TEST(Clipper2NextTriangulationContextTests, ReleaseReturnsRetainedStorage) {
    next::internal::triangulation_context context;
    auto* first = next::internal::create_triangulation_vertex(context, {0, 0});
    auto* second = next::internal::create_triangulation_vertex(context, {10, 10});
    static_cast<void>(next::internal::create_triangulation_edge(
        context, first, second, next::internal::triangulation_edge_kind::ascend));
    ASSERT_GT(context.vertex_pool.retained_capacity(), 0U);
    ASSERT_GT(context.edge_pool.retained_capacity(), 0U);

    context.release();

    EXPECT_EQ(context.vertex_pool.retained_capacity(), 0U);
    EXPECT_EQ(context.edge_pool.retained_capacity(), 0U);
    EXPECT_EQ(context.triangle_pool.retained_capacity(), 0U);
    EXPECT_EQ(context.vertices.capacity(), 0U);
    EXPECT_EQ(context.edges.capacity(), 0U);
    EXPECT_EQ(context.triangles.capacity(), 0U);
}
