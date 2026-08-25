#include <gtest/gtest.h>

#include "path_equivalence.h"
#include "support/test_paths.h"

#include "clipper2/clipper.h"
#include "clipper2next/geometry/algorithms.h"

#include <string>
#include <tuple>
#include <vector>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;
namespace test = clipper2next::tests;

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, TrimCollinearClosedMatchesLegacy) {
    const auto path = test::path64({0, 0, 50, 0, 100, 0, 100, 50, 100, 100, 0, 100, 0, 50});

    const auto expected = legacy::TrimCollinear(oracle::to_legacy_path(path), false);
    const auto actual = next::trim_collinear(path, false);

    EXPECT_EQ(oracle::to_next_path(expected), actual);
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, TrimCollinearOpenMatchesLegacy) {
    const auto path = test::path64({0, 0, 50, 0, 100, 0, 130, 40, 160, 80, 220, 80});

    const auto expected = legacy::TrimCollinear(oracle::to_legacy_path(path), true);
    const auto actual = next::trim_collinear(path, true);

    EXPECT_EQ(oracle::to_next_path(expected), actual);
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, SimplifyPathMatchesLegacy) {
    const auto path = test::path64({0, 0, 20, 1, 40, 0, 60, 20, 80, 40, 100, 41, 120, 40});

    const auto expected = legacy::SimplifyPath(oracle::to_legacy_path(path), 3.0, false);
    const auto actual = next::simplify_path(path, 3.0, false);

    EXPECT_EQ(oracle::to_next_path(expected), actual);
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, SimplifyPathsMatchesLegacy) {
    const next::Paths64 paths{test::path64({0, 0, 20, 1, 40, 0, 60, 20, 80, 40}),
                              test::path64({0, 100, 20, 101, 40, 100, 60, 120, 80, 140})};

    const auto expected = legacy::SimplifyPaths(oracle::to_legacy_paths(paths), 3.0, false);
    const auto actual = next::simplify_paths(paths, 3.0, false);

    EXPECT_EQ(oracle::to_next_paths(expected), actual);
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, RamerDouglasPeuckerMatchesLegacy) {
    const auto path = test::path64({0, 0, 20, 1, 40, 0, 60, 30, 80, 60, 100, 61, 120, 60});

    const auto expected = legacy::RamerDouglasPeucker(oracle::to_legacy_path(path), 5.0);
    const auto actual = next::ramer_douglas_peucker(path, 5.0);

    EXPECT_EQ(oracle::to_next_path(expected), actual);
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, EllipseMatchesLegacy) {
    const next::Point64 center{100, 120};

    const auto expected = legacy::Ellipse(legacy::Point64{center.x, center.y}, 80.0, 40.0, 32U);
    const auto actual = next::make_ellipse(center, 80.0, 40.0, 32U);

    EXPECT_EQ(oracle::to_next_path(expected), actual);
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, EllipseParameterSweepMatchesLegacy) {
    const std::vector<next::Point64> centers{{0, 0}, {100, -80}, {-1'000'000, 2'000'000}};
    const std::vector<std::tuple<double, double, std::size_t>> parameters{
        {12.0, 0.0, 0U}, {80.0, 40.0, 16U}, {125.0, 75.0, 64U}, {250.0, 15.0, 96U}};

    for (const auto& center : centers) {
        for (const auto& [radius_x, radius_y, steps] : parameters) {
            SCOPED_TRACE("center=(" + std::to_string(center.x) + "," + std::to_string(center.y) +
                         ") rx=" + std::to_string(radius_x) + " ry=" + std::to_string(radius_y) +
                         " steps=" + std::to_string(steps));
            const auto expected =
                legacy::Ellipse(legacy::Point64{center.x, center.y}, radius_x, radius_y, steps);
            const auto actual = next::make_ellipse(center, radius_x, radius_y, steps);
            EXPECT_EQ(oracle::to_next_path(expected), actual);
        }
    }
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, PathContainsPathMatchesLegacy) {
    const auto outer = test::path64({0, 0, 200, 0, 200, 200, 0, 200});
    const auto inner = test::path64({50, 50, 150, 50, 150, 150, 50, 150});
    const auto crossing = test::path64({50, 50, 250, 50, 250, 150, 50, 150});

    EXPECT_EQ(
        legacy::Path2ContainsPath1(oracle::to_legacy_path(inner), oracle::to_legacy_path(outer)),
        next::path_contains_path(inner, outer));
    EXPECT_EQ(
        legacy::Path2ContainsPath1(oracle::to_legacy_path(crossing), oracle::to_legacy_path(outer)),
        next::path_contains_path(crossing, outer));
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests,
     TrimCollinearDuplicateBoundaryCasesMatchLegacy) {
    const std::vector<next::Path64> cases{
        test::path64({0, 0, 50, 0, 50, 0, 100, 0, 100, 100, 0, 100, 0, 0}),
        test::path64({0, 0, 40, 0, 80, 0, 120, 40, 160, 80, 200, 80, 200, 80}),
        test::path64({-100, -100, 0, -100, 100, -100, 100, 0, 100, 100, 0, 100, -100, 100})};

    for (std::size_t index = 0; index < cases.size(); ++index) {
        for (const auto is_open : {true, false}) {
            SCOPED_TRACE("case=" + std::to_string(index) + " open=" + std::to_string(is_open));
            const auto expected =
                legacy::TrimCollinear(oracle::to_legacy_path(cases[index]), is_open);
            const auto actual = next::trim_collinear(cases[index], is_open);
            EXPECT_EQ(oracle::to_next_path(expected), actual);
        }
    }
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, SimplifyAndRdpGeneratedCorpusMatchesLegacy) {
    const std::vector<next::Path64> cases{
        test::path64({0, 0, 10, 1, 20, -1, 30, 0, 40, 20, 50, 40, 60, 39, 70, 40}),
        test::path64({0, 100, 25, 104, 50, 101, 75, 120, 100, 140, 125, 141, 150, 139}),
        test::path64({-100, 0, -60, 2, -20, -2, 20, 0, 60, 45, 100, 90, 140, 88})};
    const double tolerances[] = {1.0, 3.0, 8.0};

    for (std::size_t index = 0; index < cases.size(); ++index) {
        for (const auto tolerance : tolerances) {
            SCOPED_TRACE("case=" + std::to_string(index) +
                         " tolerance=" + std::to_string(tolerance));
            const auto expected_simplified =
                legacy::SimplifyPath(oracle::to_legacy_path(cases[index]), tolerance, false);
            const auto actual_simplified = next::simplify_path(cases[index], tolerance, false);
            EXPECT_EQ(oracle::to_next_path(expected_simplified), actual_simplified);

            const auto expected_rdp =
                legacy::RamerDouglasPeucker(oracle::to_legacy_path(cases[index]), tolerance);
            const auto actual_rdp = next::ramer_douglas_peucker(cases[index], tolerance);
            EXPECT_EQ(oracle::to_next_path(expected_rdp), actual_rdp);
        }
    }
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests, PathContainsBoundaryCasesMatchLegacy) {
    const auto outer = test::path64({0, 0, 200, 0, 200, 200, 0, 200});
    const std::vector<next::Path64> candidates{test::path64({0, 50, 100, 50, 100, 120, 0, 120}),
                                               test::path64({50, 0, 150, 0, 150, 80, 50, 80}),
                                               test::path64({200, 50, 260, 50, 260, 120, 200, 120}),
                                               test::path64({-10, 50, 40, 50, 40, 120, -10, 120})};

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(legacy::Path2ContainsPath1(oracle::to_legacy_path(candidates[index]),
                                             oracle::to_legacy_path(outer)),
                  next::path_contains_path(candidates[index], outer));
    }
}

TEST(Clipper2NextDifferentialGeometryAlgorithmsTests,
     HighMagnitudeSimplifyAndRdpCorpusMatchesLegacy) {
    constexpr int64_t base = 1'000'000'000'000;
    const std::vector<next::Path64> cases{test::path64({base,
                                                     base,
                                                     base + 25,
                                                     base + 1,
                                                     base + 50,
                                                     base - 1,
                                                     base + 75,
                                                     base,
                                                     base + 100,
                                                     base + 60,
                                                     base + 125,
                                                     base + 120,
                                                     base + 150,
                                                     base + 121}),
                                          test::path64({-base,
                                                     base,
                                                     -base + 40,
                                                     base,
                                                     -base + 80,
                                                     base,
                                                     -base + 120,
                                                     base + 80,
                                                     -base + 160,
                                                     base + 160,
                                                     -base + 220,
                                                     base + 160}),
                                          test::path64({base,
                                                     -base,
                                                     base + 30,
                                                     -base + 2,
                                                     base + 60,
                                                     -base - 2,
                                                     base + 90,
                                                     -base,
                                                     base + 120,
                                                     -base + 45,
                                                     base + 150,
                                                     -base + 90})};
    const double tolerances[] = {1.0, 6.0, 25.0};

    for (std::size_t index = 0; index < cases.size(); ++index) {
        for (const auto tolerance : tolerances) {
            SCOPED_TRACE("case=" + std::to_string(index) +
                         " tolerance=" + std::to_string(tolerance));
            const auto expected_simplified =
                legacy::SimplifyPath(oracle::to_legacy_path(cases[index]), tolerance, false);
            const auto actual_simplified = next::simplify_path(cases[index], tolerance, false);
            EXPECT_EQ(oracle::to_next_path(expected_simplified), actual_simplified);

            const auto expected_rdp =
                legacy::RamerDouglasPeucker(oracle::to_legacy_path(cases[index]), tolerance);
            const auto actual_rdp = next::ramer_douglas_peucker(cases[index], tolerance);
            EXPECT_EQ(oracle::to_next_path(expected_rdp), actual_rdp);
        }
    }
}
