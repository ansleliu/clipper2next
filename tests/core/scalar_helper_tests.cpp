#include <gtest/gtest.h>

#include "clipper2next/core/point.h"
#include "clipper2next/geometry/line_intersections.h"
#include "clipper2next/geometry/path_transforms.h"
#include "geometry/private/scalar_helpers.h"
#include "support/test_paths.h"

#include <cmath>
#include <cfenv>
#include <limits>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextScalarHelperTests, FloatingPointCoordinateConversionIsDefinedAtLimits) {
    const auto maximum = (std::numeric_limits<int64_t>::max)();
    const auto minimum = (std::numeric_limits<int64_t>::min)();

    EXPECT_EQ(geotypes::coordinateCast<int64_t>((std::numeric_limits<double>::max)()), maximum);
    EXPECT_EQ(geotypes::coordinateCast<int64_t>(-(std::numeric_limits<double>::max)()), minimum);
    EXPECT_EQ(geotypes::coordinateCast<int64_t>(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_EQ((next::Point64{maximum, maximum} + next::Point64{1, 1}).x, maximum);
    EXPECT_EQ((next::Point64{minimum, minimum} - next::Point64{1, 1}).x, minimum);
    EXPECT_EQ((-next::Point64{minimum, minimum}).x, maximum);
}

TEST(Clipper2NextScalarHelperTests, RectangleArithmeticIsDefinedAtLimits) {
    const auto maximum = (std::numeric_limits<int64_t>::max)();
    const auto minimum = (std::numeric_limits<int64_t>::min)();
    const next::Rect64 extreme{minimum, minimum, maximum, maximum};

    EXPECT_EQ(extreme.width(), maximum);
    EXPECT_EQ(extreme.height(), maximum);
    EXPECT_EQ(next::with_width(next::Rect64{maximum, 0, maximum, 1}, 1).right, maximum);
    EXPECT_EQ(next::with_width(next::Rect64{0, 0, 0, 1}, 1.75).right, 1);
    EXPECT_EQ(next::with_width(next::Rect64{minimum, 0, minimum, 1}, 0x1p63).right, 0);
    EXPECT_EQ(next::with_width(next::Rect64{minimum, 0, minimum, 1},
                               (std::numeric_limits<uint64_t>::max)())
                  .right,
              maximum);
    EXPECT_EQ(next::with_width(next::Rect64{maximum, 0, maximum, 1}, minimum).right, -1);
    EXPECT_EQ(next::with_width(next::Rect<uint32_t>{5, 0, 5, 1}, -10).right, 0U);
    EXPECT_EQ(next::scaled(next::Rect64{1, 0, 2, 1},
                           (std::numeric_limits<double>::max)())
                  .right,
              maximum);
}

TEST(Clipper2NextScalarHelperTests, PublicGeometryHelpersAreDefinedAtCoordinateLimits) {
    const auto maximum = (std::numeric_limits<int64_t>::max)();
    const auto minimum = (std::numeric_limits<int64_t>::min)();

    EXPECT_EQ(next::translate(next::Path64{{maximum, minimum}}, 1, -1).front(),
              (next::Point64{maximum, minimum}));
    EXPECT_EQ(next::reflect_point(next::Point64{minimum, minimum},
                                  next::Point64{maximum, maximum}),
              (next::Point64{maximum, maximum}));
    EXPECT_EQ(next::round_to_even_int64((std::numeric_limits<double>::max)()), maximum);
    EXPECT_TRUE(next::is_collinear(next::Point64{minimum, minimum},
                                   next::Point64{0, 0},
                                   next::Point64{maximum, maximum}));
    EXPECT_GT(next::cross_product_sign(next::Point64{minimum, minimum},
                                       next::Point64{maximum, minimum},
                                       next::Point64{maximum, maximum}),
              0);
    EXPECT_LT(next::cross_product_sign(next::Point64{minimum, minimum},
                                       next::Point64{minimum, maximum},
                                       next::Point64{maximum, maximum}),
              0);
}

TEST(Clipper2NextScalarHelperTests, ReflectionSaturatesTheCombinedExpression) {
    constexpr auto maximum = (std::numeric_limits<int64_t>::max)();
    constexpr auto minimum = (std::numeric_limits<int64_t>::lowest)();

    EXPECT_EQ(next::reflect_point(next::Point64{maximum, minimum},
                                  next::Point64{maximum, minimum}),
              (next::Point64{maximum, minimum}));
    EXPECT_EQ(next::reflect_point(next::Point64{maximum - 2, minimum + 2},
                                  next::Point64{maximum - 1, minimum + 1}),
              (next::Point64{maximum, minimum}));
}

TEST(Clipper2NextScalarHelperTests, RoundToEvenConversionIsDefinedAcrossFloatingDomain) {
    constexpr auto maximum = (std::numeric_limits<int64_t>::max)();
    constexpr auto minimum = (std::numeric_limits<int64_t>::lowest)();
    constexpr auto upper_bound = 0x1p63;
    constexpr auto lower_bound = -0x1p63;

    EXPECT_EQ(next::round_to_even_int64(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_EQ(next::round_to_even_int64(std::numeric_limits<double>::infinity()), maximum);
    EXPECT_EQ(next::round_to_even_int64(-std::numeric_limits<double>::infinity()), minimum);
    EXPECT_EQ(next::round_to_even_int64(upper_bound), maximum);
    EXPECT_EQ(next::round_to_even_int64(lower_bound), minimum);

    const auto just_below_upper = std::nextafter(upper_bound, 0.0);
    const auto just_above_lower = std::nextafter(lower_bound, 0.0);
    EXPECT_EQ(next::round_to_even_int64(just_below_upper),
              static_cast<int64_t>(just_below_upper));
    EXPECT_EQ(next::round_to_even_int64(just_above_lower),
              static_cast<int64_t>(just_above_lower));
    EXPECT_EQ(next::round_to_even_int64(1.5), 2);
    EXPECT_EQ(next::round_to_even_int64(2.5), 2);
    EXPECT_EQ(next::round_to_even_int64(3.5), 4);
    EXPECT_EQ(next::round_to_even_int64(-1.5), -2);
    EXPECT_EQ(next::round_to_even_int64(-2.5), -2);
    EXPECT_EQ(next::round_to_even_int64(-3.5), -4);
}

TEST(Clipper2NextScalarHelperTests, RoundToEvenIgnoresFloatingPointEnvironment) {
    const auto original_mode = std::fegetround();
    ASSERT_NE(original_mode, -1);
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        EXPECT_EQ(next::round_to_even_int64(1.5), 2);
        EXPECT_EQ(next::round_to_even_int64(2.5), 2);
        EXPECT_EQ(next::round_to_even_int64(-1.5), -2);
        EXPECT_EQ(next::round_to_even_int64(-2.5), -2);
        EXPECT_EQ((next::Point64{
                      geotypes::coordinateCast<int64_t>(2.5),
                      geotypes::coordinateCast<int64_t>(-2.5)}),
                  (next::Point64{2, -2}));
    }
    ASSERT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextScalarHelperTests, FastLineIntersectionPreservesLegacyTruncation) {
    next::Point64 intersection;

    ASSERT_TRUE(next::line_intersection_point(
        next::Point64{0, 0}, next::Point64{2, 1}, next::Point64{0, 1}, next::Point64{2, 0}, intersection));
    EXPECT_EQ(intersection, (next::Point64{1, 0}));
}

TEST(Clipper2NextScalarHelperTests, PreciseLineIntersectionSupportsIntegralPointTypes) {
    using Point32 = next::Point<int32_t>;
    Point32 intersection;

    ASSERT_TRUE(next::line_intersection_point(Point32{0, 0},
                                              Point32{10, 10},
                                              Point32{0, 10},
                                              Point32{10, 0},
                                              intersection,
                                              next::predicate_policy{next::precision_mode::precise}));
    EXPECT_EQ(intersection, (Point32{5, 5}));
}

TEST(Clipper2NextScalarHelperTests, PreciseLineIntersectionPreservesUnsignedNegativeDelta) {
    using PointU32 = next::Point<uint32_t>;
    PointU32 intersection;

    ASSERT_TRUE(next::line_intersection_point(PointU32{0, 0},
                                              PointU32{10, 10},
                                              PointU32{0, 0},
                                              PointU32{10, 0},
                                              intersection,
                                              next::predicate_policy{next::precision_mode::precise}));
    EXPECT_EQ(intersection, (PointU32{0, 0}));
}

TEST(Clipper2NextScalarHelperTests, ClosestPointUsesTheFullIntegralCoordinateDelta) {
    using Point32 = next::Point<int32_t>;
    constexpr auto maximum = (std::numeric_limits<int32_t>::max)();
    constexpr auto minimum = (std::numeric_limits<int32_t>::lowest)();
    const Point32 segment_start{maximum, 0};
    const Point32 segment_end{minimum, 0};

    EXPECT_EQ(next::closest_point_on_segment(segment_end, segment_start, segment_end), segment_end);
}

TEST(Clipper2NextScalarHelperTests, PathTransformPreservesLegacyTruncation) {
    const next::PathD source{{1.75, -1.75}};

    const auto transformed = next::transform_path<int64_t>(source);

    ASSERT_EQ(transformed.size(), 1U);
    EXPECT_EQ(transformed.front(), (next::Point64{1, -1}));
}

TEST(Clipper2NextScalarHelperTests, TranslatePathsMatchesPublicScalarHelper) {
    const auto paths = next::Paths64{
        test::path64({0, 0, 10, 0, 10, 10, 0, 10}),
    };

    const auto expected = next::translate(paths, 5, -3);
    const auto actual = next::internal::translate_paths64(paths, 5, -3);

    EXPECT_EQ(expected, actual);
}

TEST(Clipper2NextScalarHelperTests, BoundsMatchesPublicScalarHelper) {
    const auto paths = next::Paths64{
        test::path64({0, 0, 10, 0, 10, 10, 0, 10}),
        test::path64({-5, -6, -1, -6, -1, -2, -5, -2}),
    };

    const auto expected = next::bounds(paths);
    const auto actual = next::internal::bounds_of(paths);

    EXPECT_EQ(expected.left, actual.left);
    EXPECT_EQ(expected.top, actual.top);
    EXPECT_EQ(expected.right, actual.right);
    EXPECT_EQ(expected.bottom, actual.bottom);
}

TEST(Clipper2NextScalarHelperTests, AcceleratedBoundsMatchesScalarBounds) {
    const next::Path64 path{{0, 0}, {10, 20}, {-5, 30}};

    const auto scalar_bounds = next::bounds(path);
    const auto accelerated_bounds = next::internal::bounds(path);

    EXPECT_EQ(accelerated_bounds.left, scalar_bounds.left);
    EXPECT_EQ(accelerated_bounds.top, scalar_bounds.top);
    EXPECT_EQ(accelerated_bounds.right, scalar_bounds.right);
    EXPECT_EQ(accelerated_bounds.bottom, scalar_bounds.bottom);
}

TEST(Clipper2NextScalarHelperTests, BoundsMatchesScalarForLargeMixedCoordinates) {
    const next::Path64 path{
        {-9'000'000'000LL, 7'000'000'000LL},
        {4'500'000'000LL, -8'000'000'000LL},
        {12'000'000'000LL, 3'000'000'000LL},
        {-2'000'000'000LL, 11'000'000'000LL},
    };
    const next::Paths64 paths{
        path,
        next::Path64{
            {-15'000'000'000LL, -1'000'000'000LL},
            {-14'000'000'000LL, 2'000'000'000LL},
            {-13'000'000'000LL, -3'000'000'000LL},
        },
    };

    const auto public_path_bounds = next::bounds(path);
    const auto internal_path_bounds = next::internal::bounds(path);
    const auto public_paths_bounds = next::bounds(paths);
    const auto internal_paths_bounds = next::internal::bounds_of(paths);

    EXPECT_EQ(internal_path_bounds.left, public_path_bounds.left);
    EXPECT_EQ(internal_path_bounds.top, public_path_bounds.top);
    EXPECT_EQ(internal_path_bounds.right, public_path_bounds.right);
    EXPECT_EQ(internal_path_bounds.bottom, public_path_bounds.bottom);
    EXPECT_EQ(internal_paths_bounds.left, public_paths_bounds.left);
    EXPECT_EQ(internal_paths_bounds.top, public_paths_bounds.top);
    EXPECT_EQ(internal_paths_bounds.right, public_paths_bounds.right);
    EXPECT_EQ(internal_paths_bounds.bottom, public_paths_bounds.bottom);
}

TEST(Clipper2NextScalarHelperTests, BoundsReturnsInvalidRectForEmptyInputs) {
    EXPECT_EQ(next::bounds(next::Path64{}), next::Rect64::invalid_rect());
    EXPECT_EQ(next::bounds(next::Paths64{}), next::Rect64::invalid_rect());
    EXPECT_EQ(next::bounds(next::Paths64{next::Path64{}}), next::Rect64::invalid_rect());
}

TEST(Clipper2NextScalarHelperTests, MidpointMatchesNaiveDivisionForMixedSigns) {
    // midpointCoordinate must reproduce (a + b) / 2 with truncation toward
    // zero for every sign combination.
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(3, 2), 2);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(2, 3), 2);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(-3, -2), -2);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(-3, 2), 0);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(3, -2), 0);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(5, -6), 0);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(6, -5), 0);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(-5, 6), 0);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(-6, 5), 0);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(-1, 1), 0);
    EXPECT_EQ(geotypes::midpointCoordinate<int64_t>(7, 7), 7);
}

TEST(Clipper2NextScalarHelperTests, RectMidpointUsesTruncationTowardZero) {
    const next::Rect64 rect{-6, -5, 5, 6};

    const auto mid = rect.midpoint();

    EXPECT_EQ(mid.x, 0);
    EXPECT_EQ(mid.y, 0);
}

TEST(Clipper2NextScalarHelperTests, MidpointSurvivesCoordinateExtremes) {
    constexpr auto near_max = (std::numeric_limits<int64_t>::max)() - 2;
    constexpr auto near_min = std::numeric_limits<int64_t>::lowest() + 2;

    // The naive (a + b) / 2 overflows for both of these.
    EXPECT_EQ(geotypes::midpointCoordinate(near_max, near_max - 2), near_max - 1);
    EXPECT_EQ(geotypes::midpointCoordinate(near_min, near_min + 2), near_min + 1);

    const auto mid = next::midpoint(next::Point64{near_max, near_max},
                                    next::Point64{near_max - 4, near_max - 4});
    EXPECT_EQ(mid.x, near_max - 2);
    EXPECT_EQ(mid.y, near_max - 2);
}
