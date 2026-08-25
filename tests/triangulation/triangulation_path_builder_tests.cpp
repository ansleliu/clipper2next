#include <gtest/gtest.h>

#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_path_builder.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationPathBuilderTests, RejectsEmptyAndCollinearInput) {
    next::internal::triangulation_context empty_context;
    EXPECT_FALSE(next::internal::add_triangulation_paths(empty_context, {}));

    next::internal::triangulation_context collinear_context;
    next::internal::add_triangulation_path(collinear_context,
                                           next::Path64{{0, 0}, {5, 0}, {10, 0}, {15, 0}});

    EXPECT_TRUE(collinear_context.vertices.empty());
    EXPECT_TRUE(collinear_context.edges.empty());
}

TEST(Clipper2NextTriangulationPathBuilderTests, CreatesSquareBoundary) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
    };

    EXPECT_TRUE(next::internal::add_triangulation_paths(context, paths));

    EXPECT_GE(context.vertices.size(), 4U);
    EXPECT_GE(context.edges.size(), 4U);
    EXPECT_NE(context.lowermost_vertex, nullptr);
    EXPECT_FALSE(context.local_minima.empty());
}
