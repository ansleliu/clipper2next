#include "clipper2next/geometry/translate.h"
#include "clipper2next/geometry.h"
#include "clipper2next/geometry/algorithms.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <limits>
#include <bit>
#include <cerrno>
#include <cfenv>
#include "clipper2next/geometry/path_transforms.h"

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextGeometryAlgorithmsTests, TrimCollinearClosedPathKeepsCorners) {
    const auto path = test::path64({0, 0, 50, 0, 100, 0, 100, 50, 100, 100, 0, 100, 0, 50});

    const auto trimmed = next::trim_collinear(path, false);

    EXPECT_EQ(trimmed, test::path64({0, 0, 100, 0, 100, 100, 0, 100}));
}

TEST(Clipper2NextGeometryAlgorithmsTests, TrimCollinearOpenPathKeepsEndpoints) {
    const auto path = test::path64({0, 0, 50, 0, 100, 0, 100, 50});

    const auto trimmed = next::trim_collinear(path, true);

    EXPECT_EQ(trimmed, test::path64({0, 0, 100, 0, 100, 50}));
}

TEST(Clipper2NextGeometryAlgorithmsTests, TrimCollinearHandlesFullInt64CoordinateRange) {
    constexpr auto minimum = (std::numeric_limits<int64_t>::min)();
    constexpr auto maximum = (std::numeric_limits<int64_t>::max)();
    const next::Path64 path{
        {minimum, minimum}, {0, 0}, {maximum, maximum}, {minimum, maximum}};

    EXPECT_EQ(next::trim_collinear(path, false),
              (next::Path64{{minimum, minimum}, {maximum, maximum}, {minimum, maximum}}));
}

TEST(Clipper2NextGeometryAlgorithmsTests, RamerDouglasPeuckerKeepsShapeExtremes) {
    const auto path = test::path64({0, 0, 10, 0, 20, 0, 20, 10, 20, 20});

    const auto simplified = next::ramer_douglas_peucker(path, 1.0);

    EXPECT_EQ(simplified, test::path64({0, 0, 20, 0, 20, 20}));
}

TEST(Clipper2NextGeometryAlgorithmsTests, RamerDouglasPeuckerLeavesInputForNanTolerance) {
    const auto path = test::path64({0, 0, 10, 0, 20, 0, 30, 0, 40, 0});

    EXPECT_EQ(next::ramer_douglas_peucker(
                  path, std::numeric_limits<double>::quiet_NaN()),
              path);
}

TEST(Clipper2NextGeometryAlgorithmsTests, RamerDouglasPeuckerHandlesDeepPartitionsIteratively) {
    next::Path64 path;
    path.reserve(4097U);
    for (int64_t x = 0; x < 4097; ++x) { path.push_back(next::Point64{x, x % 2}); }

    EXPECT_EQ(next::ramer_douglas_peucker(path, 0.0), path);
}

TEST(Clipper2NextGeometryAlgorithmsTests, SimplifyPathsProcessesEachPathIndependently) {
    const next::Paths64 paths{
        test::path64({0, 0, 10, 1, 20, 0, 20, 20}),
        test::path64({100, 0, 110, 0, 120, 0, 120, 20}),
    };

    const auto simplified = next::simplify_paths(paths, 2.0, false);

    ASSERT_EQ(simplified.size(), 2U);
    EXPECT_EQ(simplified[0].front(), paths[0].front());
    EXPECT_EQ(simplified[0].back(), paths[0].back());
    EXPECT_LT(simplified[0].size(), paths[0].size());
    EXPECT_EQ(simplified[1], test::path64({100, 0, 120, 0, 120, 20}));
}

TEST(Clipper2NextGeometryAlgorithmsTests, MakeEllipseUsesRequestedCenterRadiusAndSteps) {
    const auto ellipse = next::make_ellipse(next::Point64{10, 20}, 4.0, 2.0, 4U);

    EXPECT_EQ(ellipse, test::path64({14, 20, 10, 22, 6, 20, 10, 18}));
}

TEST(Clipper2NextGeometryAlgorithmsTests, MakeEllipseRejectsNonFiniteGeometry) {
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();

    EXPECT_TRUE(next::make_ellipse(next::Point64{}, infinity).empty());
    EXPECT_TRUE(next::make_ellipse(next::Point64{}, nan).empty());
    EXPECT_TRUE(next::make_ellipse(next::Point64{}, 10.0, infinity).empty());
}

TEST(Clipper2NextGeometryAlgorithmsTests, MakeEllipseRejectsCoordinatesOutsideInt64) {
    const auto huge_radius = (std::numeric_limits<double>::max)();

    EXPECT_TRUE(next::make_ellipse(next::Point64{}, huge_radius, 1.0, 4U).empty());
    EXPECT_TRUE(next::make_ellipse(next::Point64{(std::numeric_limits<int64_t>::max)(), 0},
                                   1024.0,
                                   1.0,
                                   4U)
                    .empty());
}

TEST(Clipper2NextGeometryAlgorithmsTests, PathContainsPathRequiresInteriorContainment) {
    const auto outer = test::path64({0, 0, 200, 0, 200, 200, 0, 200});
    const auto inner = test::path64({50, 50, 150, 50, 150, 150, 50, 150});
    const auto crossing = test::path64({50, 50, 250, 50, 250, 150, 50, 150});
    const auto boundary_only = test::path64({0, 0, 100, 0, 200, 0});

    EXPECT_TRUE(next::path_contains_path(inner, outer));
    EXPECT_FALSE(next::path_contains_path(crossing, outer));
    EXPECT_FALSE(next::path_contains_path(boundary_only, outer));
}


TEST(Clipper2NextGeometryAlgorithmsTests, TransformPathMatchesScalarAcrossMixedCoordinateStates) {
    auto environment = std::fenv_t{};
    ASSERT_EQ(std::fegetenv(&environment), 0);
    struct restore_environment {
        std::fenv_t* value;
        ~restore_environment() { std::fesetenv(value); }
    } restore{&environment};
    const auto nan = (std::numeric_limits<double>::quiet_NaN)();
    const auto inf = (std::numeric_limits<double>::infinity)();
    const auto tiny = (std::numeric_limits<double>::denorm_min)();
    const next::PathD input{
        {2.5, -2.5}, {std::nextafter(0.5, 0.0), std::nextafter(-0.5, 0.0)},
        {nan, inf}, {-inf, -0.0}, {0x1p63, -0x1p63}, {tiny, -tiny},
    };
    for (const int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        SCOPED_TRACE(mode);
        ASSERT_EQ(std::fesetround(mode), 0);
        auto expected = next::Path64(input.size());
        std::feclearexcept(FE_ALL_EXCEPT);
        errno = EDOM;
        for (std::size_t i = 0; i < input.size(); ++i) {
            expected[i].x = geotypes::coordinateCast<std::int64_t>(
                input[i].x, geotypes::CoordinateRounding::NearestAwayFromZero);
            expected[i].y = geotypes::coordinateCast<std::int64_t>(
                input[i].y, geotypes::CoordinateRounding::NearestAwayFromZero);
        }
        const auto expected_flags = std::fetestexcept(FE_ALL_EXCEPT);
        const auto expected_errno = errno;
        std::feclearexcept(FE_ALL_EXCEPT);
        errno = EDOM;
        const auto actual = next::transform_path<std::int64_t>(input);
        EXPECT_EQ(std::fetestexcept(FE_ALL_EXCEPT), expected_flags);
        EXPECT_EQ(errno, expected_errno);
        EXPECT_EQ(std::fegetround(), mode);
        EXPECT_EQ(actual, expected);
    }
}


TEST(Clipper2NextGeometryAlgorithmsTests, TranslateInt64SaturatesEachCoordinateIndependently) {
    constexpr auto low = (std::numeric_limits<std::int64_t>::lowest)();
    constexpr auto high = (std::numeric_limits<std::int64_t>::max)();
    const next::Path64 input{{high, low}, {low, high}, {0, -1}, {1, 0}};
    const next::Path64 expected{{high, low}, {low + 1, high - 1}, {1, -2}, {2, -1}};
    EXPECT_EQ(next::translate(input, std::int64_t{1}, std::int64_t{-1}), expected);
    EXPECT_EQ(next::translate<std::int64_t>(input, 1, -1), expected);
    const next::Path64 extreme{{high, low}, {low, high}};
    const next::Path64 extreme_expected{{-1, -1}, {low, high}};
    EXPECT_EQ(next::translate(extreme, low, high), extreme_expected);
    EXPECT_EQ(next::translate<std::int64_t>(extreme, low, high), extreme_expected);
}
