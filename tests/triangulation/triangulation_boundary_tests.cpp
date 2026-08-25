#include <gtest/gtest.h>

#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_path_builder.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationBoundaryTests, CreatesOuterAndHoleBoundaries) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
        next::Path64{{25, 25}, {25, 75}, {75, 75}, {75, 25}},
    };

    EXPECT_TRUE(next::internal::build_triangulation_boundary(context, paths));

    EXPECT_GE(context.vertices.size(), 8U);
    EXPECT_GE(context.edges.size(), 8U);
    EXPECT_NE(context.lowermost_vertex, nullptr);
}

TEST(Clipper2NextTriangulationBoundaryTests, RejectsRepeatedPointDegenerateInput) {
    next::internal::triangulation_context context;

    next::internal::add_triangulation_path(context, next::Path64{{0, 0}, {0, 0}, {0, 0}, {0, 0}});

    EXPECT_TRUE(context.vertices.empty());
    EXPECT_TRUE(context.edges.empty());
}
