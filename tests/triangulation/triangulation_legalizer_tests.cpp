#include <gtest/gtest.h>

#include "triangulation/private/triangulation_legalizer.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationLegalizerTests, PredicatesClassifyTurnsAndInCircle) {
    EXPECT_TRUE(next::internal::LeftTurning({0, 0}, {0, 10}, {10, 0}));
    EXPECT_TRUE(next::internal::RightTurning({0, 0}, {10, 0}, {0, 10}));
    EXPECT_GT(next::internal::InCircleTest({0, 0}, {10, 0}, {0, 10}, {1, 1}), 0.0);
}
