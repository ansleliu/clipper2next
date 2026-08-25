#include <gtest/gtest.h>

#include "offset/private/offset_geometry.h"
#include "support/test_paths.h"

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetGeometryTests, UnitNormalMatchesLegacyOrientation) {
    const auto normal = next::internal::unit_normal({0, 0}, {10, 0});

    EXPECT_DOUBLE_EQ(normal.x, 0.0);
    EXPECT_DOUBLE_EQ(normal.y, -1.0);
}

TEST(Clipper2NextOffsetGeometryTests, BuildNormalsClosesThePath) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10, 0, 10});

    const auto normals = next::internal::build_normals(path);

    ASSERT_EQ(normals.size(), path.size());
    EXPECT_DOUBLE_EQ(normals.front().x, 0.0);
    EXPECT_DOUBLE_EQ(normals.front().y, -1.0);
    EXPECT_DOUBLE_EQ(normals.back().x, -1.0);
    EXPECT_DOUBLE_EQ(normals.back().y, -0.0);
}

TEST(Clipper2NextOffsetGeometryTests, ArcParametersUseLegacyDefaultToleranceWhenUnset) {
    const auto parameters = next::internal::make_arc_parameters(100.0, 0.0);

    EXPECT_GT(parameters.steps_per_rad, 0.0);
    EXPECT_GT(parameters.step_sin, 0.0);
    EXPECT_LT(parameters.step_cos, 1.0);
}
