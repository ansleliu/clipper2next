#include <gtest/gtest.h>

#include "clipper2next/rectclip.h"
#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_unprepared_runner.h"
#include "rectclip/private/rectclip_edges.h"
#include "rectclip/private/rectclip_execution_context.h"
#include "rectclip/private/rectclip_facade_runner.h"
#include "rectclip/private/rectclip_line_executor.h"
#include "rectclip/private/rectclip_path_builder.h"
#include "rectclip/private/rectclip_polygon_executor.h"
#include "support/test_paths.h"

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

TEST(Clipper2NextRectClipExecutorTests, LineExecutorReferencesStorage) {
    next::internal::rectclip_context storage{{0, 0, 10, 10}};
    next::internal::rectclip_line_executor executor{storage};

    EXPECT_EQ(&executor.storage(), &storage);
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerClipsPolygonMode) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{-5, -5}, {15, -5}, {15, 15}, {-5, 15}},
    };

    const auto clipped =
        next::internal::execute_rectclip(rect, paths, next::internal::rectclip_mode::polygons);

    ASSERT_EQ(clipped.size(), 1U);
    EXPECT_EQ(next::bounds(clipped.front()), rect);
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerClipsLineMode) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{-5, 5}, {5, 5}, {15, 5}},
    };

    const auto clipped =
        next::internal::execute_rectclip(rect, paths, next::internal::rectclip_mode::lines);

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
        next::internal::execute_rectclip(rect, paths, next::internal::rectclip_mode::lines);

    EXPECT_EQ(clipped, paths);
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerReturnsFullyContainedLinePathsWithBounds) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{1, 1}, {5, 9}, {9, 9}},
        next::Path64{{2, 8}, {8, 2}},
    };
    std::vector<next::Rect64> path_bounds;
    next::internal::rectclip_path_bounds_summary summary;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds_if_in_range(
        paths, path_bounds, summary));
    const next::internal::rectclip_path_bounds_view bounds_view{
        path_bounds,
        summary,
    };
    const auto clipped = next::internal::execute_rectclip(
        rect, paths, bounds_view, next::internal::rectclip_mode::lines);

    EXPECT_EQ(clipped, paths);
}

TEST(Clipper2NextRectClipExecutorTests, FacadeRunnerDeduplicatesContainedLinePath) {
    const next::Rect64 rect{0, 0, 10, 10};
    const next::Paths64 paths{
        next::Path64{{1, 1}, {1, 1}, {5, 9}, {9, 9}, {9, 9}},
        next::Path64{{3, 3}, {3, 3}},
    };
    std::vector<next::Rect64> path_bounds;
    next::internal::rectclip_path_bounds_summary summary;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds_if_in_range(
        paths, path_bounds, summary));
    const next::internal::rectclip_path_bounds_view bounds_view{
        path_bounds,
        summary,
    };
    const auto clipped = next::internal::execute_rectclip(
        rect, paths, bounds_view, next::internal::rectclip_mode::lines);

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
        next::internal::execute_rectclip(rect, paths, next::internal::rectclip_mode::polygons);

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
    next::internal::rectclip_path_bounds_summary summary;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds_if_in_range(
        paths, path_bounds, summary));
    const next::internal::rectclip_path_bounds_view bounds_view{
        path_bounds,
        summary,
    };
    const auto expected = next::internal::execute_rectclip(
        rect, paths, bounds_view, next::internal::rectclip_mode::polygons);
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
    next::internal::rectclip_path_bounds_summary summary;

    ASSERT_TRUE(next::internal::build_rectclip_path_bounds_if_in_range(
        paths, path_bounds, summary));
    const next::internal::rectclip_path_bounds_view bounds_view{path_bounds, summary};
    const auto expected = next::internal::execute_rectclip(
        rect, paths, bounds_view, next::internal::rectclip_mode::polygons);
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

TEST(Clipper2NextRectClipExecutorTests, PathBoundsSummaryTracksCombinedBoundsAndMinimumSizes) {
    const next::Paths64 paths{
        next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        next::Path64{{20, 5}, {25, 5}},
    };
    std::vector<next::Rect64> path_bounds;
    next::internal::rectclip_path_bounds_summary summary;

    const auto in_range =
        next::internal::build_rectclip_path_bounds_if_in_range(paths, path_bounds, summary);

    ASSERT_TRUE(in_range);
    ASSERT_EQ(path_bounds.size(), 2U);
    EXPECT_EQ(path_bounds[0], next::Rect64(0, 0, 10, 10));
    EXPECT_EQ(path_bounds[1], next::Rect64(20, 5, 25, 5));
    EXPECT_TRUE(summary.has_bounds);
    EXPECT_EQ(summary.combined_bounds, next::Rect64(0, 0, 25, 10));
    EXPECT_FALSE(summary.all_paths_have_polygon_minimum_size);
    EXPECT_TRUE(summary.all_paths_have_line_minimum_size);
}

TEST(Clipper2NextRectClipExecutorTests, PolygonExecutorBuildsClippedRectanglePath) {
    next::internal::rectclip_context storage{{0, 0, 10, 10}};
    next::internal::rectclip_polygon_executor executor{storage};
    next::internal::rectclip_execution_context execution{storage};
    const next::Path64 path{{-5, -5}, {15, -5}, {15, 15}, {-5, 15}};
    storage.path_bounds = next::bounds(path);

    executor.execute_path(execution, path);
    next::internal::check_edges(storage.results, storage.edges, storage.rect);
    for (size_t index = 0; index < 4; ++index) {
        next::internal::tidy_edges(
            index, storage.edges[index * 2], storage.edges[index * 2 + 1], storage.results);
    }

    ASSERT_FALSE(storage.results.empty());
    auto* node = storage.results.front().get();
    const auto clipped = next::internal::build_polygon_path(node);

    ASSERT_EQ(clipped.size(), 4U);
    EXPECT_EQ(next::bounds(clipped), storage.rect);
}

TEST(Clipper2NextRectClipExecutorTests, LineExecutorBuildsClippedSegmentPath) {
    next::internal::rectclip_context storage{{0, 0, 10, 10}};
    next::internal::rectclip_line_executor executor{storage};
    next::internal::rectclip_execution_context execution{storage};

    executor.execute_path(execution, next::Path64{{-5, 5}, {15, 5}});

    ASSERT_FALSE(storage.results.empty());
    auto* node = storage.results.front().get();
    const auto clipped = next::internal::build_line_path(node);

    ASSERT_EQ(clipped.size(), 2U);
    EXPECT_EQ(clipped.front(), next::Point64(0, 5));
    EXPECT_EQ(clipped.back(), next::Point64(10, 5));
}
