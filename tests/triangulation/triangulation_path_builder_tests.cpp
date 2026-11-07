#include <gtest/gtest.h>

#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_path_builder.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationPathBuilderTests, RejectsEmptyAndCollinearInput) {
    next::internal::triangulation_context empty_context;
    EXPECT_FALSE(empty_context.source_paths.append({}));

    next::internal::triangulation_context collinear_context;
    EXPECT_FALSE(collinear_context.source_paths.append(
        {next::Path64{{0, 0}, {5, 0}, {10, 0}, {15, 0}}}));

    EXPECT_TRUE(collinear_context.source_paths.empty());
}

TEST(Clipper2NextTriangulationPathBuilderTests, PreservesSourceBeforeNormalization) {
    next::internal::triangulation_context context;
    next::Paths64 paths{
        next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        next::Path64{{1, 1}, {2, 1}, {3, 1}},
        next::Path64{{20, 20}, {30, 20}, {30, 20}, {30, 30}, {20, 30}, {20, 20}},
    };
    const auto unchanged = paths;
    const next::Paths64 expected{paths[0], {{20, 20}, {30, 20}, {30, 30}, {20, 30}}};

    EXPECT_TRUE(context.source_paths.append(paths));

    EXPECT_EQ(paths, unchanged);
    ASSERT_EQ(context.source_paths.size(), expected.size());
    EXPECT_EQ(context.source_paths.point_count(), 8U);
    for (std::size_t i = 0; i != expected.size(); ++i) {
        const auto source = context.source_paths[i];
        EXPECT_EQ((next::Path64{source.begin(), source.end()}), expected[i]);
    }
    paths[0][0] = {999, 999};
    EXPECT_EQ(context.source_paths[0].front(), expected[0].front());
    context.clear();
    EXPECT_TRUE(context.source_paths.empty());
    EXPECT_EQ(context.source_paths.point_count(), 0U);
    EXPECT_TRUE(context.source_paths.append(unchanged));
    EXPECT_EQ(context.source_paths.point_count(), 8U);
    EXPECT_TRUE(context.vertices.empty());
    EXPECT_TRUE(context.edges.empty());
    EXPECT_TRUE(context.local_minima.empty());
}
