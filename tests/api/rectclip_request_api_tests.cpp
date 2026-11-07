#include "clipper2next/rectclip.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextRectClipRequestApiTests, MoveTransfersCompleteSnapshotAndDefaultsSource) {
    const next::prepared_rect_clip_request64 empty;
    const next::rect_clip_request64 request{
        {3, 5, 40, 50}, {{{10, 10}, {20, 10}, {20, 20}, {10, 20}}}};
    const auto expected = next::prepare_rect_clip_request(request);
    auto source = expected;
    auto moved = next::prepared_rect_clip_request64{std::move(source)};
    EXPECT_EQ(moved.request().rect, request.rect);
    EXPECT_EQ(moved.request().paths, request.paths);
    EXPECT_EQ(next::rect_clip(moved).paths, next::rect_clip(expected).paths);
    EXPECT_EQ(source.request().rect, empty.request().rect);
    EXPECT_EQ(source.request().paths, empty.request().paths);
    EXPECT_EQ(next::rect_clip(source).paths, next::rect_clip(empty).paths);
    EXPECT_TRUE(next::rect_clip(source.request()).paths.empty());
    source = expected;
    moved = std::move(source);
    EXPECT_EQ(moved.request().rect, request.rect);
    EXPECT_EQ(moved.request().paths, request.paths);
    EXPECT_EQ(next::rect_clip(moved).paths, next::rect_clip(expected).paths);
    EXPECT_EQ(source.request().rect, empty.request().rect);
    EXPECT_EQ(source.request().paths, empty.request().paths);
    EXPECT_EQ(next::rect_clip(source).paths, next::rect_clip(empty).paths);
    EXPECT_TRUE(next::rect_clip(source.request()).paths.empty());
}

TEST(Clipper2NextRectClipRequestApiTests, DefaultAndSelfAssignmentRemainLegal) {
    next::prepared_rect_clip_request64 empty;
    EXPECT_TRUE(next::rect_clip(empty).paths.empty());
    auto snapshot = next::prepare_rect_clip_request(
        {{3, 5, 40, 50}, {{{10, 10}, {20, 10}, {20, 20}, {10, 20}}}});
    const auto expected = snapshot;
    auto& alias = snapshot;
    snapshot = alias;
    EXPECT_EQ(snapshot.request().rect, expected.request().rect);
    EXPECT_EQ(snapshot.request().paths, expected.request().paths);
    EXPECT_EQ(next::rect_clip(snapshot).paths, next::rect_clip(expected).paths);
    snapshot = std::move(alias);
    EXPECT_EQ(next::rect_clip(snapshot).paths, next::rect_clip(snapshot.request()).paths);
}

TEST(Clipper2NextRectClipRequestApiTests, FactoriesRejectInvalidCoordinates) {
    const next::Paths64 invalid{{{next::MAX_COORD + 1, 0}, {10, 0}, {10, 10}}};
    EXPECT_THROW((void)next::prepare_rect_clip_request({{0, 0, 100, 100}, invalid}),
                 next::clipper_error);
    EXPECT_THROW((void)next::prepare_rect_clip_request({{0, 0, next::MAX_COORD + 1, 100}, {}}),
                 next::clipper_error);
    EXPECT_THROW((void)next::prepare_immutable_rect_clip_paths(invalid), next::clipper_error);
}

TEST(Clipper2NextRectClipRequestApiTests, ImmutableSnapshotsSupportMoveCopyAndConcurrentReads) {
    const next::Rect64 rect{0, 0, 40, 40};
    const next::Paths64 paths{{{10, 10}, {20, 10}, {20, 20}, {10, 20}}};
    const auto expected = next::prepare_immutable_rect_clip_paths(paths);
    const auto reference = next::rect_clip(rect, expected).paths;
    ASSERT_FALSE(reference.empty());
    auto source = expected;
    auto moved = next::immutable_rect_clip_paths64{std::move(source)};
    EXPECT_TRUE(next::rect_clip(rect, source).paths.empty());
    EXPECT_EQ(next::rect_clip(rect, moved).paths, reference);
    source = expected;
    moved = std::move(source);
    EXPECT_TRUE(next::rect_clip(rect, source).paths.empty());
    EXPECT_EQ(next::rect_clip(rect, moved).paths, reference);
    auto& alias = moved;
    moved = alias;
    EXPECT_EQ(next::rect_clip(rect, moved).paths, reference);
    moved = std::move(alias);
    EXPECT_EQ(next::rect_clip(rect, moved).paths, reference);
    std::array<std::thread, 4U> workers;
    std::array<std::atomic<bool>, 4U> matched;
    for (std::size_t index = 0; index < workers.size(); ++index) {
        matched[index].store(true);
        workers[index] = std::thread{[&, index] {
            for (int iteration = 0; iteration < 20; ++iteration) {
                if (next::rect_clip(rect, moved).paths != reference) {
                    matched[index].store(false);
                }
            }
        }};
    }
    for (auto& worker : workers) { worker.join(); }
    for (const auto& match : matched) { EXPECT_TRUE(match.load()); }
}

namespace {

[[nodiscard]] auto dense_rect_path(int64_t left, int64_t top, int64_t right, int64_t bottom)
    -> next::Path64 {
    next::Path64 path;
    path.reserve(32U);
    const auto width_step = (right - left) / 8;
    const auto height_step = (bottom - top) / 8;
    for (int64_t index = 0; index < 8; ++index) {
        path.emplace_back(left + index * width_step, top);
    }
    for (int64_t index = 0; index < 8; ++index) {
        path.emplace_back(right, top + index * height_step);
    }
    for (int64_t index = 0; index < 8; ++index) {
        path.emplace_back(right - index * width_step, bottom);
    }
    for (int64_t index = 0; index < 8; ++index) {
        path.emplace_back(left, bottom - index * height_step);
    }
    return path;
}

}  // namespace

TEST(Clipper2NextRectClipRequestApiTests, PreparedRectClipMatchesUnpreparedRectClip) {
    next::rect_clip_request64 request;
    request.rect = next::Rect64{0, 0, 100, 100};
    request.paths = next::Paths64{
        test::path64({10, 10, 40, 10, 40, 40, 10, 40}),
        test::path64({90, 90, 120, 90, 120, 120, 90, 120}),
        test::path64({-20, -20, -10, -20, -10, -10, -20, -10}),
    };

    const auto prepared = next::prepare_rect_clip_request(request);
    const auto unprepared_result = next::rect_clip(request);
    const auto prepared_result = next::rect_clip(prepared);

    EXPECT_EQ(prepared_result.paths, unprepared_result.paths);
}

TEST(Clipper2NextRectClipRequestApiTests, ImmutableRectClipPathsMatchStrictRectClip) {
    const auto rect = next::Rect64{0, 0, 100, 100};
    next::Paths64 paths{
        test::path64({10, 10, 40, 10, 40, 40, 10, 40}),
        test::path64({90, 90, 120, 90, 120, 120, 90, 120}),
        test::path64({-20, -20, -10, -20, -10, -10, -20, -10}),
    };

    const auto immutable = next::prepare_immutable_rect_clip_paths(paths);
    const auto immutable_result = next::rect_clip(rect, immutable);
    const auto strict_result = next::rect_clip(next::rect_clip_request64{rect, paths});

    EXPECT_EQ(immutable_result.paths, strict_result.paths);
}

TEST(Clipper2NextRectClipRequestApiTests, ImmutableRectClipPathsSnapshotSourcePaths) {
    const auto rect = next::Rect64{0, 0, 100, 100};
    next::Paths64 paths{
        test::path64({10, 10, 40, 10, 40, 40, 10, 40}),
    };

    const auto immutable = next::prepare_immutable_rect_clip_paths(paths);
    const auto immutable_before = next::rect_clip(rect, immutable);
    ASSERT_EQ(immutable_before.paths.size(), 1U);

    paths[0] = test::path64({200, 200, 240, 200, 240, 240, 200, 240});
    const auto immutable_after = next::rect_clip(rect, immutable);
    const auto strict_after = next::rect_clip(next::rect_clip_request64{rect, paths});

    EXPECT_EQ(immutable_after.paths, immutable_before.paths);
    EXPECT_NE(strict_after.paths, immutable_before.paths);
}

TEST(Clipper2NextRectClipRequestApiTests, ImmutableRectClipDefaultHandleReturnsEmpty) {
    next::immutable_rect_clip_paths64 immutable;
    const auto result = next::rect_clip(next::Rect64{0, 0, 100, 100}, immutable);
    EXPECT_TRUE(result.paths.empty());
}

TEST(Clipper2NextRectClipRequestApiTests, UnpreparedRectClipReflectsInBoundsMutation) {
    next::rect_clip_request64 request;
    request.rect = next::Rect64{0, 0, 100, 100};
    request.paths = next::Paths64{
        dense_rect_path(10, 10, 40, 40),
        dense_rect_path(90, 90, 120, 120),
    };

    const auto warmup_result = next::rect_clip(request);
    ASSERT_FALSE(warmup_result.paths.empty());

    request.paths[0][1].x = 35;
    request.paths[0][2].y = 35;
    request.paths[1][0].x = 95;
    request.paths[1][8].x = 115;

    const auto unprepared_result = next::rect_clip(request);
    const auto prepared_result = next::rect_clip(next::prepare_rect_clip_request(request));
    EXPECT_EQ(unprepared_result.paths, prepared_result.paths);
}

TEST(Clipper2NextRectClipRequestApiTests, UnpreparedRectClipReflectsOutOfBoundsMutation) {
    next::rect_clip_request64 request;
    request.rect = next::Rect64{0, 0, 100, 100};
    request.paths = next::Paths64{
        dense_rect_path(10, 10, 40, 40),
        dense_rect_path(90, 90, 120, 120),
    };

    const auto warmup_result = next::rect_clip(request);
    ASSERT_FALSE(warmup_result.paths.empty());

    request.paths[0][2].x = 70;
    request.paths[0][2].y = 85;
    request.paths[1][0].x = 130;

    const auto unprepared_result = next::rect_clip(request);
    const auto prepared_result = next::rect_clip(next::prepare_rect_clip_request(request));
    EXPECT_EQ(unprepared_result.paths, prepared_result.paths);
}

TEST(Clipper2NextRectClipRequestApiTests, UnpreparedRectClipRecomputesContainmentAfterMutation) {
    next::rect_clip_request64 request;
    request.rect = next::Rect64{0, 0, 100, 100};
    request.paths = next::Paths64{
        dense_rect_path(10, 10, 40, 40),
        dense_rect_path(50, 50, 80, 80),
    };

    const auto warmup_result = next::rect_clip(request);
    ASSERT_EQ(warmup_result.paths.size(), 2U);
    const auto repeated_result = next::rect_clip(request);
    ASSERT_EQ(repeated_result.paths, warmup_result.paths);

    request.paths[0][1].x = 150;
    request.paths[0][2].x = 150;

    const auto unprepared_result = next::rect_clip(request);
    const auto prepared_result = next::rect_clip(next::prepare_rect_clip_request(request));
    EXPECT_EQ(unprepared_result.paths, prepared_result.paths);
    EXPECT_NE(unprepared_result.paths, warmup_result.paths);
}

TEST(Clipper2NextRectClipRequestApiTests, MultipleLinesRetainEarlierKernelNodesUntilMaterialization) {
    next::rect_clip_lines_request64 request;
    request.rect = {-10, -10, 110, 110};
    request.lines = {{{-50, 50}, {25, 50}, {150, 50}},
                     {{30, -80}, {30, 30}, {30, 180}},
                     {{-40, -40}, {140, 140}}};
    const next::Paths64 expected{{{-10, 50}, {25, 50}, {110, 50}},
                                 {{30, -10}, {30, 30}, {30, 110}},
                                 {{-10, -10}, {110, 110}}};
    EXPECT_EQ(next::rect_clip_lines(request).paths, expected);
    next::rect_clip_result64 result;
    next::rect_clip_lines_into(request, result);
    EXPECT_EQ(result.paths, expected);
    next::rect_clip_lines_into(request, result);
    EXPECT_EQ(result.paths, expected);
}
