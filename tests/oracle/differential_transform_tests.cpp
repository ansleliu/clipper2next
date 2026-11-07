#include <gtest/gtest.h>

#include "path_equivalence.h"
#include "support/test_paths.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

#include <cfenv>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;
namespace test = clipper2next::tests;

TEST(Clipper2NextDifferentialTransformTests, TranslatePathMatchesLegacy) {
    const auto path = test::path64({-10, 5, 20, -15, 35, 40, -5, 45});

    const auto expected =
        legacy::TranslatePath(oracle::to_legacy_path(path), int64_t{123}, int64_t{-77});
    const auto actual = next::translate(path, int64_t{123}, int64_t{-77});

    EXPECT_EQ(oracle::to_next_path(expected), actual);
}

TEST(Clipper2NextDifferentialTransformTests, TranslatePointMatchesLegacyAtHalfInteger) {
    const auto original_mode = std::fegetround();
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        EXPECT_EQ(std::fesetround(mode), 0);
        for (const auto point : {next::Point64{0, 0}, next::Point64{3, -3},
                                 next::Point64{-3, 3}}) {
            for (const auto delta : {0.5, -0.5, 1.5, -1.5, 2.5, -2.5}) {
                const auto expected = legacy::TranslatePoint(
                    legacy::Point64{point.x, point.y}, delta, -delta);
                EXPECT_EQ((next::Point64{expected.x, expected.y}),
                          next::translate_point(point, delta, -delta));
                EXPECT_EQ(std::fegetround(), mode);
            }
        }
    }
    EXPECT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextDifferentialTransformTests, ClosestPointMatchesLegacyAcrossFloatingEnvironments) {
    const auto original_mode = std::fegetround();
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        EXPECT_EQ(std::fesetround(mode), 0);
        for (const auto direction : {-1, 1}) {
            const next::Point64 start{0, 0};
            const next::Point64 end{2 * direction, 2 * direction};
            for (const auto distance : {1, 3}) {
                const next::Point64 point{distance * direction, 0};
                const auto expected = legacy::GetClosestPointOnSegment(
                    legacy::Point64{point.x, point.y}, legacy::Point64{start.x, start.y},
                    legacy::Point64{end.x, end.y});
                EXPECT_EQ((next::Point64{expected.x, expected.y}),
                          next::closest_point_on_segment(point, start, end));
                EXPECT_EQ(std::fegetround(), mode);
            }
        }
    }
    EXPECT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextDifferentialTransformTests, ScaleIntegralRectMatchesLegacyTruncation) {
    const next::Rect64 rect{-3, -1, 5, 7};
    const auto original_mode = std::fegetround();
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        EXPECT_EQ(std::fesetround(mode), 0);
        for (const auto scale : {0.5, 1.5, -0.5, -1.5}) {
            auto expected = legacy::Rect64{rect.left, rect.top, rect.right, rect.bottom};
            expected.Scale(scale);
            const next::Rect64 expected_rect{
                expected.left, expected.top, expected.right, expected.bottom};
            EXPECT_EQ(expected_rect, next::scaled(rect, scale));
            const auto converted = legacy::ScaleRect<int64_t, int64_t>(
                legacy::Rect64{rect.left, rect.top, rect.right, rect.bottom}, scale);
            EXPECT_EQ((next::Rect64{converted.left, converted.top, converted.right, converted.bottom}),
                      next::scale_rect<int64_t>(rect, scale));
            EXPECT_EQ(std::fegetround(), mode);
        }
    }
    EXPECT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextDifferentialTransformTests, ScaleFloatingRectMatchesLegacyNearestAway) {
    const next::RectD rect{-3.75, -1.25, 5.125, 7.875};
    const auto original_mode = std::fegetround();
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        EXPECT_EQ(std::fesetround(mode), 0);
        for (const auto scale : {0.5, 1.0, 2.0, -0.5, -2.0, 100.0}) {
            const auto expected = legacy::ScaleRect<int64_t, double>(
                legacy::RectD{rect.left, rect.top, rect.right, rect.bottom}, scale);
            EXPECT_EQ((next::Rect64{expected.left, expected.top, expected.right, expected.bottom}),
                      next::scale_rect<int64_t>(rect, scale));
            EXPECT_EQ(std::fegetround(), mode);
        }
    }
    EXPECT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextDifferentialTransformTests, TransformPathMatchesLegacyForFractionalCoordinates) {
    const next::PathD path{{0.5, -0.5}, {0.75, -0.75}, {1.75, -1.75},
                           {2.5, -2.5}, {8.125, -8.125}};
    const legacy::PathD legacy_path{{0.5, -0.5}, {0.75, -0.75}, {1.75, -1.75},
                                    {2.5, -2.5}, {8.125, -8.125}};
    const auto original_mode = std::fegetround();
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        EXPECT_EQ(std::fesetround(mode), 0);
        const auto expected = legacy::TransformPath<int64_t, double>(legacy_path);
        EXPECT_EQ(oracle::to_next_path(expected), next::transform_path<int64_t>(path));
        const auto expected_many = legacy::TransformPaths<int64_t, double>(
            legacy::PathsD{legacy_path, {}});
        EXPECT_EQ(oracle::to_next_paths(expected_many),
                  next::transform_paths<int64_t>(next::PathsD{path, {}}));
        EXPECT_EQ(std::fegetround(), mode);
    }
    EXPECT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextDifferentialTransformTests, ScalePathMatchesLegacyAtHalfInteger) {
    const next::PathD path{{0.25, -1.5}, {2.5, 3.0}, {-4.75, 8.125}};
    const legacy::PathD legacy_path{{0.25, -1.5}, {2.5, 3.0}, {-4.75, 8.125}};
    int legacy_error = 0;

    const auto expected = legacy::ScalePath<int64_t, double>(legacy_path, 100.0, legacy_error);
    const auto actual = next::scale_path<int64_t>(path, next::scale_request{100.0, 100.0});

    ASSERT_EQ(legacy_error, 0);
    ASSERT_TRUE(actual.has_value());
    const auto exact_away = test::path64({25, -150, 250, 300, -475, 813});
    EXPECT_EQ(actual.value(), exact_away);
    EXPECT_EQ(oracle::to_next_path(expected), actual.value());
}

TEST(Clipper2NextDifferentialTransformTests, ScalePathsMatchesLegacyAcrossFloatingEnvironments) {
    const next::PathsD paths{
        {},
        {{0.5, -0.5}, {2.5, -2.5}, {8.125, -8.125}, {-4.75, 4.75}, {0.005, -0.005}},
    };
    legacy::PathsD legacy_paths;
    for (const auto& path : paths) {
        legacy_paths.emplace_back();
        for (const auto& point : path) { legacy_paths.back().emplace_back(point.x, point.y); }
    }
    const auto original_mode = std::fegetround();
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        EXPECT_EQ(std::fesetround(mode), 0);
        for (const auto scale : {1.0, 2.0, 0.5, 100.0, -1.0, -100.0}) {
            int legacy_error = 0;
            const auto expected = legacy::ScalePaths<int64_t, double>(
                legacy_paths, scale, legacy_error);
            const auto actual = next::scale_paths<int64_t>(paths, next::scale_request{scale, scale});
            EXPECT_EQ(legacy_error, 0);
            EXPECT_TRUE(actual.has_value());
            if (actual) { EXPECT_EQ(oracle::to_next_paths(expected), actual.value()); }
            EXPECT_EQ(std::fegetround(), mode);
        }
    }
    EXPECT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextDifferentialTransformTests, ScalePathMatchesLegacyAwayFromHalfInteger) {
    const next::PathD path{{0.25, -1.5}, {2.5, 3.0}, {-4.75, 8.12}};
    const legacy::PathD legacy_path{{0.25, -1.5}, {2.5, 3.0}, {-4.75, 8.12}};
    int legacy_error = 0;

    const auto expected = legacy::ScalePath<int64_t, double>(legacy_path, 100.0, legacy_error);
    const auto actual = next::scale_path<int64_t>(path, next::scale_request{100.0, 100.0});

    ASSERT_EQ(legacy_error, 0);
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(oracle::to_next_path(expected), actual.value());
}
