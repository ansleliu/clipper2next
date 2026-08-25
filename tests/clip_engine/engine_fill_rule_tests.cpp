#include "clip/engine/private/engine_fill_rule.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineFillRuleTests, ClosedContributionRejectsNonBoundaryWindCounts) {
    EXPECT_FALSE(next::internal::is_contributing_closed(
        next::ClipType::Union, next::FillRule::NonZero, next::PathType::Subject, 2, 0));

    EXPECT_TRUE(next::internal::is_contributing_closed(
        next::ClipType::Union, next::FillRule::NonZero, next::PathType::Subject, 1, 0));
}

TEST(Clipper2NextEngineFillRuleTests, DifferenceFlipsClosedClipPathContribution) {
    EXPECT_TRUE(next::internal::is_contributing_closed(
        next::ClipType::Difference, next::FillRule::NonZero, next::PathType::Subject, 1, 0));

    EXPECT_FALSE(next::internal::is_contributing_closed(
        next::ClipType::Difference, next::FillRule::NonZero, next::PathType::Clip, 1, 0));

    EXPECT_FALSE(next::internal::is_contributing_closed(
        next::ClipType::Difference, next::FillRule::NonZero, next::PathType::Subject, 1, 1));

    EXPECT_TRUE(next::internal::is_contributing_closed(
        next::ClipType::Difference, next::FillRule::NonZero, next::PathType::Clip, 1, 1));
}

TEST(Clipper2NextEngineFillRuleTests, SignedFillRulesUseSignedOppositeWindCount) {
    EXPECT_TRUE(next::internal::is_contributing_closed(
        next::ClipType::Intersection, next::FillRule::Positive, next::PathType::Subject, 1, 2));

    EXPECT_FALSE(next::internal::is_contributing_closed(
        next::ClipType::Intersection, next::FillRule::Positive, next::PathType::Subject, 1, -2));

    EXPECT_TRUE(next::internal::is_contributing_closed(
        next::ClipType::Union, next::FillRule::Negative, next::PathType::Subject, -1, 0));
}

TEST(Clipper2NextEngineFillRuleTests, OpenContributionMatchesClipAndSubjectMembership) {
    EXPECT_TRUE(next::internal::is_contributing_open(
        next::ClipType::Intersection, next::FillRule::EvenOdd, 0, 1));

    EXPECT_TRUE(
        next::internal::is_contributing_open(next::ClipType::Union, next::FillRule::EvenOdd, 0, 0));

    EXPECT_FALSE(
        next::internal::is_contributing_open(next::ClipType::Union, next::FillRule::EvenOdd, 1, 0));

    EXPECT_TRUE(next::internal::is_contributing_open(
        next::ClipType::Difference, next::FillRule::Negative, 0, 1));
}
