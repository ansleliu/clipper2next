#include "clipper2next/clip.h"
#include "clipper2next/geometry.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <cfenv>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextClipRequestApiTests, ClipReturnsClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::clip(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClipRequestApiTests, ClipRequestCanSelectPreciseIntersectionPolicy) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.options.intersection_policy = next::predicate_policy{next::precision_mode::precise};
    request.subjects = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };
    request.clips = next::Paths64{
        test::path64({50, -25, 125, -25, 125, 75, 50, 75}),
    };

    const auto result = next::clip_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().closed.size(), 1U);
    EXPECT_EQ(next::bounds(result.value().closed), next::Rect64(50, 0, 100, 75));
}

TEST(Clipper2NextClipRequestApiTests, ClipUnionWithEmptySubjectsReturnsClipClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.clips = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::clip(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed[0].size(), 4U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClipRequestApiTests, ClipXorWithEmptySubjectsReturnsClipClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Xor;
    request.fill_rule = next::FillRule::NonZero;
    request.clips = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::clip(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed[0].size(), 4U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClipRequestApiTests, ClipReturnsOpenPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.open_subjects = {{{0, 50}, {100, 50}}};
    request.clips = {{{25, 25}, {75, 25}, {75, 75}, {25, 75}}};

    const auto result = next::clip(request);

    EXPECT_TRUE(result.closed.empty());
    ASSERT_EQ(result.open.size(), 1U);
    EXPECT_EQ(result.open[0], next::Path64({{25, 50}, {75, 50}}));
}

TEST(Clipper2NextClipRequestApiTests, ClipTreeReturnsHierarchy) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {
        {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        {{2, 2}, {2, 8}, {8, 8}, {8, 2}},
    };

    const auto result = next::clip_tree(request);

    EXPECT_TRUE(result.open.empty());
    ASSERT_EQ(result.tree.count(), 1U);
    const auto outer = result.tree.child(result.tree.root(), 0);
    ASSERT_EQ(result.tree.count(outer), 1U);
    const auto hole = result.tree.child(outer, 0);
    EXPECT_TRUE(result.tree.is_hole(hole));
    EXPECT_NEAR(result.tree.area(), 64.0, 0.001);
}

TEST(Clipper2NextClipRequestApiTests, ClipExecutionIsIndependentOfCallerRoundingMode) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{0, 0}, {1, 2}, {2, 0}}};
    request.clips = {{{-10, 1}, {10, 1}, {10, 3}, {-10, 3}}};

    const auto original_mode = std::fegetround();
    ASSERT_NE(original_mode, -1);
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const auto expected = next::clip(request);

    for (const auto mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        EXPECT_EQ(next::clip(request).closed, expected.closed);
        EXPECT_EQ(std::fegetround(), mode);
    }
    ASSERT_EQ(std::fesetround(original_mode), 0);
}
