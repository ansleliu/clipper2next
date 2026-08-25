#include <gtest/gtest.h>

#include "rectclip/private/rectclip_traversal.h"

namespace next = clipper2next;

TEST(Clipper2NextRectClipTraversalTests, TraversalHelpersKeepClockwiseContract) {
    using next::internal::rect_location;

    EXPECT_EQ(next::internal::adjacent_location(rect_location::Left, true), rect_location::Top);
    EXPECT_EQ(next::internal::adjacent_location(rect_location::Left, false), rect_location::Bottom);
    EXPECT_TRUE(next::internal::heading_clockwise(rect_location::Left, rect_location::Top));
    EXPECT_TRUE(next::internal::are_opposites(rect_location::Left, rect_location::Right));
    EXPECT_TRUE(next::internal::is_clockwise(
        rect_location::Left, rect_location::Right, {0, 2}, {10, 1}, {5, 5}));
    EXPECT_TRUE(next::internal::start_locations_are_clockwise(
        {rect_location::Left, rect_location::Top, rect_location::Right}));
    EXPECT_FALSE(next::internal::start_locations_are_clockwise(
        {rect_location::Left, rect_location::Bottom, rect_location::Right}));
}

TEST(Clipper2NextRectClipTraversalTests, NextExternalLocationSkipsOutsideRun) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Path64 path{{-5, 5}, {-4, 6}, {5, 6}};
    size_t index = 0;

    const auto location = next::internal::next_external_location(
        rect, path, next::internal::rect_location::Left, index, path.size() - 1);

    EXPECT_EQ(index, 2U);
    EXPECT_EQ(location, next::internal::rect_location::Inside);
}
