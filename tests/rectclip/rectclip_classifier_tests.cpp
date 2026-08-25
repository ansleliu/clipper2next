#include "rectclip/private/rectclip_classifier.h"
#include "rectclip/private/rectclip_edges.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextRectClipClassifierTests, ClassifiesPointAgainstRectBoundary) {
    const next::Rect64 rect{0, 0, 100, 100};

    EXPECT_EQ(next::internal::classify_point(rect, {-1, 50}), next::internal::rect_location::Left);
    EXPECT_EQ(next::internal::classify_point(rect, {50, -1}), next::internal::rect_location::Top);
    EXPECT_EQ(next::internal::classify_point(rect, {101, 50}),
              next::internal::rect_location::Right);
    EXPECT_EQ(next::internal::classify_point(rect, {50, 101}),
              next::internal::rect_location::Bottom);
    EXPECT_EQ(next::internal::classify_point(rect, {50, 50}),
              next::internal::rect_location::Inside);
}

TEST(Clipper2NextRectClipClassifierTests, BoundaryPointsKeepTheirOwningEdge) {
    const next::Rect64 rect{0, 0, 100, 100};

    EXPECT_EQ(next::internal::classify_point(rect, {0, 50}), next::internal::rect_location::Left);
    EXPECT_EQ(next::internal::classify_point(rect, {100, 50}),
              next::internal::rect_location::Right);
    EXPECT_EQ(next::internal::classify_point(rect, {50, 0}), next::internal::rect_location::Top);
    EXPECT_EQ(next::internal::classify_point(rect, {50, 100}),
              next::internal::rect_location::Bottom);
    EXPECT_TRUE(next::internal::is_on_rect_boundary(rect, {0, 50}));
    EXPECT_FALSE(next::internal::is_on_rect_boundary(rect, {50, 50}));
}

TEST(Clipper2NextRectClipClassifierTests, RectLocationOrderKeepsClockwiseBoundaryContract) {
    EXPECT_EQ(static_cast<int>(next::internal::rect_location::Left), 0);
    EXPECT_EQ(static_cast<int>(next::internal::rect_location::Top), 1);
    EXPECT_EQ(static_cast<int>(next::internal::rect_location::Right), 2);
    EXPECT_EQ(static_cast<int>(next::internal::rect_location::Bottom), 3);
    EXPECT_EQ(static_cast<int>(next::internal::rect_location::Inside), 4);
}

TEST(Clipper2NextRectClipClassifierTests, GetEdgesForPointReturnsCornerBitset) {
    const next::Rect64 rect{0, 0, 100, 100};

    EXPECT_EQ(next::internal::get_edges_for_point({0, 0}, rect), 3U);
    EXPECT_EQ(next::internal::get_edges_for_point({100, 100}, rect), 12U);
    EXPECT_EQ(next::internal::get_edges_for_point({50, 50}, rect), 0U);
}
