#include <gtest/gtest.h>

#include "clipper2next/geometry.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_result_builder.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationResultBuilderTests, NormalizesTriangleOrientation) {
    next::internal::triangulation_context context;
    auto* first = next::internal::create_triangulation_vertex(context, {0, 0});
    auto* second = next::internal::create_triangulation_vertex(context, {10, 0});
    auto* third = next::internal::create_triangulation_vertex(context, {0, 10});
    auto* first_edge = next::internal::create_triangulation_edge(context, first, second);
    auto* second_edge = next::internal::create_triangulation_edge(context, second, third);
    auto* third_edge = next::internal::create_triangulation_edge(context, third, first);
    auto* triangle =
        next::internal::create_triangulation_triangle(context, first_edge, second_edge, third_edge);

    const auto result = next::internal::build_triangulation_result(context);

    ASSERT_NE(triangle, nullptr);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_GT(next::area(result.front()), 0.0);
}
