#include "clipper2next/clipper.h"
#include "offset/private/offset_output.h"
#include "offset/private/offset_executor.h"
#include "geometry/private/numeric_policy.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetOutputTests, AppendPerpendicularAddsRoundedOffsetPoint) {
    next::Path64 output;

    next::internal::append_perpendicular(output, {10, 20}, {1.0, 0.0}, 2.5);

    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0], next::Point64(12, 20));
}

TEST(Clipper2NextOffsetOutputTests, AppendMiterAddsSingleCornerPoint) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    next::Path64 output;

    next::internal::append_miter(output, path, normals, 1, 0, 2.0, 0.0);

    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0], next::Point64(12, -2));
}

TEST(Clipper2NextOffsetOutputTests, AppendBevelAddsEdgeOffsetPair) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    next::Path64 output;

    next::internal::append_bevel(output, path, normals, 1, 0, 2.0);

    ASSERT_EQ(output.size(), 2U);
    EXPECT_EQ(output[0], next::Point64(10, -2));
    EXPECT_EQ(output[1], next::Point64(12, 0));
}

TEST(Clipper2NextOffsetOutputTests, AppendSquareAddsReflectedCornerPair) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    next::Path64 output;

    next::internal::append_square(output, path, normals, 1, 0, 2.0);

    ASSERT_EQ(output.size(), 2U);
    EXPECT_EQ(output[0], next::Point64(11, -2));
    EXPECT_EQ(output[1], next::Point64(12, -1));
}

TEST(Clipper2NextOffsetOutputTests, AppendRoundUsesLegacyArcParameters) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    const auto arc = next::internal::make_arc_parameters(2.0, 0.0);
    next::Path64 output;

    next::internal::append_round(output, path, normals, 1, 0, 2.0, arc, next::internal::pi / 2);

    ASSERT_GE(output.size(), 3U);
    EXPECT_EQ(output.front(), next::Point64(10, -2));
    EXPECT_EQ(output.back(), next::Point64(12, 0));
}

TEST(Clipper2NextOffsetOutputTests, ReserveEstimateCoversPolygonAndOpenPaths) {
    const auto arc = next::internal::make_arc_parameters(10.0, 0.0);

    EXPECT_EQ(next::internal::estimate_path_output_capacity(
                  4, next::JoinType::Miter, next::EndType::Polygon, arc),
              8U);
    EXPECT_GE(next::internal::estimate_path_output_capacity(
                  4, next::JoinType::Round, next::EndType::Polygon, arc),
              4U);
    EXPECT_EQ(next::internal::estimate_path_output_capacity(
                  4, next::JoinType::Bevel, next::EndType::Butt, arc),
              12U);
    EXPECT_EQ(next::internal::estimate_single_point_output_capacity(next::JoinType::Square, arc),
              4U);
    EXPECT_GE(next::internal::estimate_single_point_output_capacity(next::JoinType::Round, arc),
              1U);
}
