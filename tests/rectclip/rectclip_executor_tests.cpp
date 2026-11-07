#include <gtest/gtest.h>

#include "clipper2next/rectclip.h"
#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_unprepared_runner.h"
#include "rectclip/private/rectclip_facade_runner.h"
#include "rectclip/private/rectclip_thread_state.h"
#include "rectclip/private/rectclip_line_executor.h"
#include "rectclip/private/rectclip_polygon_executor.h"
#include "support/test_paths.h"
#include "support/rectclip_test_execution.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextRectClipExecutorTests, PolygonExecutorReferencesStorage) {
    next::internal::rectclip_context storage{{0, 0, 10, 10}};
    next::internal::rectclip_polygon_executor executor{storage};

    EXPECT_EQ(&executor.storage(), &storage);
}

TEST(Clipper2NextRectClipExecutorTests, LineExecutorBuildsClippedSequence) {
    const next::Rect64 rect{0, 0, 10, 10};
    next::internal::rectclip_line_executor executor{rect};
    next::Paths64 result;
    executor.execute_path(result, {{-5, 5}, {5, 5}, {5, 5}, {15, 5}});
    EXPECT_EQ(result, (next::Paths64{{{0, 5}, {5, 5}, {10, 5}}}));
}

TEST(Clipper2NextRectClipExecutorTests, LineSequenceDropsDegenerateContourAndPreservesCompletedPaths) {
    const next::Rect64 rect{0, 0, 10, 10};
    next::internal::rectclip_line_executor executor{rect};
    next::Paths64 result{{{1, 1}, {2, 2}}};
    executor.execute_path(result, {{0, 0}, {0, 0}});
    EXPECT_EQ(result, (next::Paths64{{{1, 1}, {2, 2}}}));
    executor.execute_path(result, {{0, 0}, {10, 0}});
    EXPECT_EQ(result, (next::Paths64{{{1, 1}, {2, 2}}, {{0, 0}, {10, 0}}}));
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerClipsPolygonMode) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{-5, -5}, {15, -5}, {15, 15}, {-5, 15}},
    };

    const auto clipped =
        test::execute_rectclip(rect, paths, test::rectclip_mode::polygons);

    ASSERT_EQ(clipped.size(), 1U);
    EXPECT_EQ(next::bounds(clipped.front()), rect);
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerClipsLineMode) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{-5, 5}, {5, 5}, {15, 5}},
    };

    const auto clipped =
        test::execute_rectclip(rect, paths, test::rectclip_mode::lines);

    ASSERT_EQ(clipped.size(), 1U);
    EXPECT_EQ(clipped.front().front(), next::Point64(0, 5));
    EXPECT_EQ(clipped.front().back(), next::Point64(10, 5));
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerReturnsFullyContainedLinePath) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{1, 1}, {5, 9}, {9, 1}},
    };

    const auto clipped =
        test::execute_rectclip(rect, paths, test::rectclip_mode::lines);

    EXPECT_EQ(clipped, paths);
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerReturnsFullyContainedLinePathsWithBounds) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{1, 1}, {5, 9}, {9, 9}},
        next::Path64{{2, 8}, {8, 2}},
    };
    std::vector<next::Rect64> path_bounds;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds(paths, true, path_bounds));
    const auto clipped = test::execute_rectclip(
        rect, paths, path_bounds, test::rectclip_mode::lines);

    EXPECT_EQ(clipped, paths);
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerDeduplicatesContainedLinePath) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{1, 1}, {1, 1}, {5, 9}, {9, 9}, {9, 9}},
        next::Path64{{3, 3}, {3, 3}},
    };
    std::vector<next::Rect64> path_bounds;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds(paths, true, path_bounds));
    const auto clipped = test::execute_rectclip(
        rect, paths, path_bounds, test::rectclip_mode::lines);

    ASSERT_EQ(clipped.size(), 1U);
    EXPECT_EQ(clipped.front(), (next::Path64{{1, 1}, {5, 9}, {9, 9}}));
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerReturnsFullyContainedPolygonPaths) {
    const next::Rect64 rect{-100, -100, 300, 300};
    const next::Paths64 paths{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
        next::Path64{{150, 20}, {250, 20}, {250, 120}, {150, 120}},
    };

    const auto clipped =
        test::execute_rectclip(rect, paths, test::rectclip_mode::polygons);

    EXPECT_EQ(clipped, paths);
}

TEST(Clipper2NextRectClipExecutorTests, OnePassPolygonRunnerMatchesPrecomputedBoundsPath) {
    const next::Rect64 rect{0, 0, 100, 100};
    const next::Paths64 paths{
        next::Path64{{10, 10}, {40, 10}, {40, 40}, {10, 40}},
        next::Path64{{-20, -20}, {-10, -20}, {-10, -10}, {-20, -10}},
        next::Path64{{90, 90}, {120, 90}, {120, 120}, {90, 120}},
        next::Path64{{-20, 50}, {50, -20}, {120, 50}, {50, 120}},
        next::Path64{{-20, -20}, {120, -20}, {120, 120}, {-20, 120}},
    };
    std::vector<next::Rect64> path_bounds;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds(paths, true, path_bounds));
    const auto expected = test::execute_rectclip(
        rect, paths, path_bounds, test::rectclip_mode::polygons);
    const auto actual = next::internal::execute_rectclip_unprepared_polygons(
        rect, paths, true);

    ASSERT_TRUE(actual.in_range);
    EXPECT_EQ(actual.paths, expected);
}

TEST(Clipper2NextRectClipExecutorTests, OnePassPolygonRunnerReportsOutOfRangeCoordinate) {
    const next::Rect64 rect{0, 0, 100, 100};
    const next::Paths64 paths{
        next::Path64{{0, 0}, {next::MAX_COORD + 1, 0}, {0, 10}},
    };

    const auto result =
        next::internal::execute_rectclip_unprepared_polygons(rect, paths, true);

    EXPECT_FALSE(result.in_range);
    EXPECT_TRUE(result.paths.empty());
}

TEST(Clipper2NextRectClipExecutorTests, OnePassPolygonRunnerRejectsInt64Extremes) {
    const next::Rect64 rect{0, 0, 100, 100};
    for (const auto coordinate : {std::numeric_limits<std::int64_t>::min(),
                                  std::numeric_limits<std::int64_t>::max()}) {
        SCOPED_TRACE(coordinate);
        const next::Paths64 paths{
            next::Path64{{10, 10}, {40, 10}, {40, 40}, {10, 40}},
            next::Path64{{0, 0}, {coordinate, 0}, {0, 10}},
        };

        const auto result =
            next::internal::execute_rectclip_unprepared_polygons(rect, paths, true);

        EXPECT_FALSE(result.in_range);
        EXPECT_TRUE(result.paths.empty());
    }
}

TEST(Clipper2NextRectClipExecutorTests,
     OnePassPolygonRunnerFallsBackForValidCoordinatesOutsideInt32) {
    const next::Rect64 rect{0, 0, 100, 100};
    constexpr auto wide_coordinate =
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1;
    const next::Paths64 paths{
        next::Path64{{10, 10}, {90, 10}, {wide_coordinate, 50}, {90, 90}, {10, 90}},
    };
    std::vector<next::Rect64> path_bounds;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds(paths, true, path_bounds));
    const auto expected = test::execute_rectclip(
        rect, paths, path_bounds, test::rectclip_mode::polygons);
    const auto actual =
        next::internal::execute_rectclip_unprepared_polygons(rect, paths, true);

    ASSERT_TRUE(actual.in_range);
    EXPECT_EQ(actual.paths, expected);
}

TEST(Clipper2NextRectClipExecutorTests,
     OnePassPolygonRunnerClearsContainedOutputOnLaterRangeFailure) {
    const next::Rect64 rect{0, 0, 100, 100};
    const next::Paths64 paths{
        next::Path64{{10, 10}, {40, 10}, {40, 40}, {10, 40}},
        next::Path64{{0, 0}, {next::MAX_COORD + 1, 0}, {0, 10}},
    };

    const auto result =
        next::internal::execute_rectclip_unprepared_polygons(rect, paths, true);

    EXPECT_FALSE(result.in_range);
    EXPECT_TRUE(result.paths.empty());
}

TEST(Clipper2NextRectClipExecutorTests,
     OnePassPolygonRunnerRejectsOutOfRangeDegeneratePath) {
    const next::Rect64 rect{0, 0, 100, 100};
    const next::Paths64 paths{
        next::Path64{{next::MIN_COORD - 1, 0}},
    };

    const auto result =
        next::internal::execute_rectclip_unprepared_polygons(rect, paths, true);

    EXPECT_FALSE(result.in_range);
    EXPECT_TRUE(result.paths.empty());
}

TEST(Clipper2NextRectClipExecutorTests,
     OnePassPolygonRunnerAcceptsInclusiveCoordinateLimits) {
    const next::Rect64 rect{
        next::MIN_COORD, next::MIN_COORD, next::MAX_COORD, next::MAX_COORD};
    const next::Paths64 paths{
        next::Path64{{next::MIN_COORD, 0},
                     {0, next::MIN_COORD},
                     {next::MAX_COORD, 0},
                     {0, next::MAX_COORD}},
    };

    const auto result =
        next::internal::execute_rectclip_unprepared_polygons(rect, paths, true);

    ASSERT_TRUE(result.in_range);
    EXPECT_EQ(result.paths, paths);
}

TEST(Clipper2NextRectClipExecutorTests, ShortBatchBoundsMatchScalarExtrema) {
    const auto points = next::Path64{
        {-17, 31}, {43, -29}, {7, 5}, {-101, 13}, {89, 97}, {3, -73}, {61, 11},
        {19, -109}, {-131, 23}, {17, 137}, {149, 7}, {-5, -157}, {163, 11},
        {-167, 13}, {29, 173}, {179, -31}, {-181, 37}, {41, -191}};
    for (std::size_t count = 0; count <= points.size(); ++count) {
        SCOPED_TRACE(count);
        const auto path = next::Path64{points.begin(), points.begin() + count};
        auto expected = next::Rect64::invalid_rect();
        if (!path.empty()) {
            expected = {path.front().x, path.front().y, path.front().x, path.front().y};
            for (const auto& point : path) {
                expected.left = (std::min)(expected.left, point.x);
                expected.top = (std::min)(expected.top, point.y);
                expected.right = (std::max)(expected.right, point.x);
                expected.bottom = (std::max)(expected.bottom, point.y);
            }
        }
        for (const bool checked : {false, true}) {
            std::vector<next::Rect64> actual;
            ASSERT_TRUE(next::internal::build_rectclip_path_bounds({path}, checked, actual));
            ASSERT_EQ(actual.size(), 1U);
            EXPECT_EQ(actual.front(), expected);
            auto single = next::Rect64::invalid_rect();
            EXPECT_EQ(next::internal::rectclip_path_bounds(path, checked, single), !path.empty());
            if (!path.empty()) { EXPECT_EQ(single, expected); }
        }
    }
    for (const auto coordinate : {(std::numeric_limits<std::int64_t>::min)(),
                                  (std::numeric_limits<std::int64_t>::max)()}) {
        auto path = next::Path64(18U, next::Point64{10, 10});
        path[1] = {coordinate, coordinate};
        auto single = next::Rect64::invalid_rect();
        ASSERT_TRUE(next::internal::rectclip_path_bounds(path, false, single));
        const auto low = (std::min)(std::int64_t{10}, coordinate);
        const auto high = (std::max)(std::int64_t{10}, coordinate);
        EXPECT_EQ(single, next::Rect64(low, low, high, high));
        EXPECT_FALSE(next::internal::rectclip_path_bounds(path, true, single));
    }
}

TEST(Clipper2NextRectClipExecutorTests, PathBoundsRetainEachValidatedPath) {
    const next::Paths64 paths{
        next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        next::Path64{{20, 5}, {25, 5}},
    };
    std::vector<next::Rect64> path_bounds;

    const auto in_range =
        next::internal::build_rectclip_path_bounds(paths, true, path_bounds);

    ASSERT_TRUE(in_range);
    ASSERT_EQ(path_bounds.size(), 2U);
    EXPECT_EQ(path_bounds[0], next::Rect64(0, 0, 10, 10));
    EXPECT_EQ(path_bounds[1], next::Rect64(20, 5, 25, 5));
}

TEST(Clipper2NextRectClipExecutorTests, DirectRunnerBuildsClippedRectanglePath) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{{{-5, -5}, {15, -5}, {15, 15}, {-5, 15}}};
    next::Paths64 result;
    std::vector<next::Rect64> bounds;
    ASSERT_TRUE(next::internal::build_rectclip_path_bounds(paths, false, bounds));
    next::internal::rectclip_operation_lease operation;
    next::internal::execute_rectclip_into(
        operation.context(rect), paths, bounds, next::internal::rectclip_mode::polygons, result);
    ASSERT_EQ(result.size(), 1U);
    ASSERT_EQ(result.front().size(), 4U);
    EXPECT_EQ(next::area(result.front()), 100);
    for (const next::Point64 corner : {next::Point64{0, 0}, {10, 0}, {10, 10}, {0, 10}}) {
        EXPECT_NE(std::ranges::find(result.front(), corner), result.front().end());
    }
}

TEST(Clipper2NextRectClipExecutorTests, DirectRunnerBuildsClippedSegmentPath) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{{{-5, 5}, {15, 5}}};
    next::Paths64 result;
    std::vector<next::Rect64> bounds;
    ASSERT_TRUE(next::internal::build_rectclip_path_bounds(paths, false, bounds));
    next::internal::execute_rectclip_lines_into(rect, paths, bounds, result);
    const next::Paths64 expected{{{0, 5}, {10, 5}}};
    EXPECT_EQ(result, expected);
}

TEST(Clipper2NextRectClipExecutorTests, RawBatchExtremaKeepCheckedAdmissionSeparate) {
    const auto low = (std::numeric_limits<std::int64_t>::min)();
    const auto high = (std::numeric_limits<std::int64_t>::max)();
    const next::Paths64 paths{{}, {{low, high}, {high, low}}};
    std::vector<next::Rect64> bounds;
    ASSERT_TRUE(next::internal::build_rectclip_path_bounds(paths, false, bounds));
    ASSERT_EQ(bounds.size(), 2U);
    EXPECT_EQ(bounds[0], next::Rect64::invalid_rect());
    EXPECT_EQ(bounds[1], next::Rect64(low, low, high, high));
    EXPECT_FALSE(next::internal::build_rectclip_path_bounds(paths, true, bounds));
    EXPECT_TRUE(bounds.empty());
}

TEST(Clipper2NextRectClipExecutorTests, UnrolledBoundsRetainSecondaryExtremaAndAdmission) {
    for (const std::size_t count : {9U,10U,11U,12U,13U,18U,26U,34U}) {
        SCOPED_TRACE(count);
        auto path = next::Path64(count, next::Point64{5,7});
        path.front() = {2,3};
        path.back() = {6,9};
        path[6] = {-101,-103};
        path[7] = {107,109};
        for (const bool checked : {false,true}) {
            std::vector<next::Rect64> bounds;
            ASSERT_TRUE(next::internal::build_rectclip_path_bounds({path}, checked, bounds));
            ASSERT_EQ(bounds.size(), 1U);
            EXPECT_EQ(bounds.front(), next::Rect64(-101,-103,107,109));
        }
        path[6].x = next::MIN_COORD - 1;
        std::vector<next::Rect64> bounds;
        ASSERT_TRUE(next::internal::build_rectclip_path_bounds({path}, false, bounds));
        ASSERT_EQ(bounds.size(), 1U);
        EXPECT_EQ(bounds.front(), next::Rect64(next::MIN_COORD-1,-103,107,109));
        EXPECT_FALSE(next::internal::build_rectclip_path_bounds({path}, true, bounds));
        EXPECT_TRUE(bounds.empty());
    }
}
