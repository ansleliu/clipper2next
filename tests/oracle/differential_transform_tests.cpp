#include <gtest/gtest.h>

#include "path_equivalence.h"
#include "support/test_paths.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

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

TEST(Clipper2NextDifferentialTransformTests, ScalePathUsesExactEvenRoundingAtHalfInteger) {
    const next::PathD path{{0.25, -1.5}, {2.5, 3.0}, {-4.75, 8.125}};
    const legacy::PathD legacy_path{{0.25, -1.5}, {2.5, 3.0}, {-4.75, 8.125}};
    int legacy_error = 0;

    const auto expected = legacy::ScalePath<int64_t, double>(legacy_path, 100.0, legacy_error);
    const auto actual = next::scale_path<int64_t>(path, next::scale_request{100.0, 100.0});

    ASSERT_EQ(legacy_error, 0);
    ASSERT_TRUE(actual.has_value());
    const auto exact_even = test::path64({25, -150, 250, 300, -475, 812});
    EXPECT_EQ(actual.value(), exact_even);
    EXPECT_NE(oracle::to_next_path(expected), exact_even);
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
