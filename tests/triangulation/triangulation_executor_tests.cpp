#include <gtest/gtest.h>

#include "triangulation/private/triangulation_executor.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationExecutorTests, TriangulatesSimpleSquare) {
    next::TriangulateResult result = next::TriangulateResult::fail;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
    };

    const auto triangles = next::internal::execute_triangulation(paths, true, result);

    EXPECT_EQ(result, next::TriangulateResult::success);
    EXPECT_FALSE(triangles.empty());
}
