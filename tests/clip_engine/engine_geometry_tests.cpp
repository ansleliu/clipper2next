#include "clip/engine/private/engine_geometry.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineGeometryTests, HorizontalDxUsesSentinelDirection) {
    const next::Point64 left{0, 10};
    const next::Point64 right{20, 10};

    EXPECT_LT(next::internal::get_dx(left, right), 0.0);
    EXPECT_GT(next::internal::get_dx(right, left), 0.0);
}

TEST(Clipper2NextEngineGeometryTests, TopXMatchesVerticalAndSlopedEdges) {
    next::internal::active_edge_node vertical;
    vertical.bottom = {10, 0};
    vertical.top_point = {10, 100};
    vertical.dx = next::internal::get_dx(vertical.bottom, vertical.top_point);

    next::internal::active_edge_node diagonal;
    diagonal.bottom = {0, 0};
    diagonal.top_point = {100, 100};
    diagonal.dx = next::internal::get_dx(diagonal.bottom, diagonal.top_point);

    EXPECT_EQ(next::internal::top_x(vertical, 50), 10);
    EXPECT_EQ(next::internal::top_x(diagonal, 50), 50);
}

TEST(Clipper2NextEngineGeometryTests, TopXUsesNearestEvenTieRounding) {
    next::internal::active_edge_node positive;
    positive.bottom = {0, 0};
    positive.top_point = {5, 2};
    positive.dx = next::internal::get_dx(positive.bottom, positive.top_point);

    next::internal::active_edge_node negative;
    negative.bottom = {0, 0};
    negative.top_point = {-5, 2};
    negative.dx = next::internal::get_dx(negative.bottom, negative.top_point);

    EXPECT_EQ(next::internal::top_x(positive, 1), 2);
    EXPECT_EQ(next::internal::top_x(negative, 1), -2);
}
