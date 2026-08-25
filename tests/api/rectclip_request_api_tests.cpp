#include "clipper2next/rectclip.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace next = clipper2next;
namespace test = clipper2next::tests;

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
