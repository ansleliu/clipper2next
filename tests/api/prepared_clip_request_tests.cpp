#include <optional>
#include <algorithm>
#include "clipper2next/batch.h"
#include "clipper2next/clip.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

namespace next = clipper2next;
namespace test = clipper2next::tests;

namespace {

auto expect_same_snapshot(const next::prepared_clip_request64& actual,
                          const next::prepared_clip_request64& expected) -> void {
    const auto& a = actual.request();
    const auto& e = expected.request();
    EXPECT_EQ(a.clip_type, e.clip_type);
    EXPECT_EQ(a.fill_rule, e.fill_rule);
    EXPECT_EQ(a.options.preserve_collinear, e.options.preserve_collinear);
    EXPECT_EQ(a.options.reverse_solution, e.options.reverse_solution);
    EXPECT_EQ(a.options.intersection_policy.mode, e.options.intersection_policy.mode);
    EXPECT_EQ(a.subjects, e.subjects);
    EXPECT_EQ(a.open_subjects, e.open_subjects);
    EXPECT_EQ(a.clips, e.clips);
    const auto& am = actual.metadata();
    const auto& em = expected.metadata();
    EXPECT_EQ(am.single_subject_rect, em.single_subject_rect);
    EXPECT_EQ(am.single_clip_rect, em.single_clip_rect);
    EXPECT_EQ(am.subject_path_count, em.subject_path_count);
    EXPECT_EQ(am.open_subject_path_count, em.open_subject_path_count);
    EXPECT_EQ(am.clip_path_count, em.clip_path_count);
    EXPECT_EQ(am.subject_point_count, em.subject_point_count);
    EXPECT_EQ(am.open_subject_point_count, em.open_subject_point_count);
    EXPECT_EQ(am.clip_point_count, em.clip_point_count);
    const auto result = next::clip(actual);
    const auto reference = next::clip(expected);
    EXPECT_EQ(result.closed, reference.closed);
    EXPECT_EQ(result.open, reference.open);
}

auto move_test_request() -> next::clip_request64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.options.preserve_collinear = false;
    request.options.reverse_solution = true;
    request.options.intersection_policy.mode = next::precision_mode::precise;
    request.subjects = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                        {{100, 0}, {110, 0}, {110, 10}, {100, 10}}};
    return request;
}

}  // namespace

TEST(Clipper2NextPreparedClipRequestTests, MoveTransfersCompleteSnapshotAndDefaultsSource) {
    const next::prepared_clip_request64 empty;
    const auto expected = next::prepare_clip_request(move_test_request());
    auto source = expected;
    auto moved = next::prepared_clip_request64{std::move(source)};
    expect_same_snapshot(moved, expected);
    expect_same_snapshot(source, empty);
    EXPECT_TRUE(next::clip(source.request()).closed.empty());
    source = expected;
    moved = std::move(source);
    expect_same_snapshot(moved, expected);
    expect_same_snapshot(source, empty);
    EXPECT_TRUE(next::clip(source.request()).closed.empty());

    auto all_kinds = move_test_request();
    all_kinds.subjects.resize(1U);
    all_kinds.open_subjects = {{{0, 20}, {10, 20}}};
    all_kinds.clips = {{{100, 0}, {110, 0}, {110, 10}, {100, 10}}};
    const auto all_expected = next::prepare_clip_request(all_kinds);
    source = all_expected;
    auto all_moved = next::prepared_clip_request64{std::move(source)};
    expect_same_snapshot(all_moved, all_expected);
    expect_same_snapshot(source, empty);
    source = all_expected;
    all_moved = std::move(source);
    expect_same_snapshot(all_moved, all_expected);
    expect_same_snapshot(source, empty);
}

TEST(Clipper2NextPreparedClipRequestTests, DefaultCopyAndSelfAssignmentRemainLegal) {
    next::prepared_clip_request64 empty;
    EXPECT_TRUE(next::clip_checked(empty));
    EXPECT_TRUE(next::clip(empty).closed.empty());
    auto snapshot = next::prepare_clip_request(move_test_request());
    const auto expected = snapshot;
    auto& alias = snapshot;
    snapshot = alias;
    expect_same_snapshot(snapshot, expected);
    snapshot = std::move(alias);
    EXPECT_TRUE(next::clip_checked(snapshot));
    expect_same_snapshot(next::prepared_clip_request64{empty}, empty);
}

TEST(Clipper2NextPreparedClipRequestTests, MovedFromTwoRectangleUnionHasNoPhantomFastResult) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    request.clips = {{{100, 0}, {110, 0}, {110, 10}, {100, 10}}};
    const auto expected = next::prepare_clip_request(request);
    ASSERT_TRUE(expected.metadata().single_subject_rect);
    ASSERT_TRUE(expected.metadata().single_clip_rect);
    auto source = expected;
    auto moved = next::prepared_clip_request64{std::move(source)};
    EXPECT_TRUE(next::clip(source.request()).closed.empty());
    EXPECT_TRUE(next::clip(source).closed.empty());
    EXPECT_EQ(next::clip(moved).closed, next::clip(expected).closed);
    source = expected;
    moved = std::move(source);
    EXPECT_TRUE(next::clip(source.request()).closed.empty());
    EXPECT_TRUE(next::clip(source).closed.empty());
    EXPECT_EQ(next::clip(moved).closed, next::clip(expected).closed);
}

TEST(Clipper2NextPreparedClipRequestTests, CopyAssignmentReplacesRectangleMetadataAndRuntime) {
    auto request = move_test_request();
    request.subjects = {{{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}}};
    request.options.preserve_collinear = true;
    auto target = next::prepare_clip_request(request);
    ASSERT_FALSE(target.metadata().single_subject_rect);
    request.subjects = {{{100, 0}, {110, 0}, {110, 10}, {100, 10}}};
    const auto source = next::prepare_clip_request(request);
    target = source;
    ASSERT_TRUE(target.metadata().single_subject_rect);
    EXPECT_EQ(target.metadata().subject_point_count, 4U);
    expect_same_snapshot(target, source);
}

TEST(Clipper2NextPreparedClipRequestTests, FactoryRejectsInvalidCoordinatesAndParameters) {
    auto request = move_test_request();
    request.subjects.front().front().x = next::MAX_COORD + 1;
    EXPECT_THROW((void)next::prepare_clip_request(request), next::clipper_error);
    request.clip_type = static_cast<next::ClipType>(999);
    try {
        (void)next::prepare_clip_request(request);
        FAIL() << "invalid parameters must precede coordinates";
    } catch (const next::clipper_error& error) {
        EXPECT_EQ(error.code(), next::clipper_error_code::invalid_argument);
    }
    request = move_test_request();
    request.fill_rule = static_cast<next::FillRule>(999);
    EXPECT_THROW((void)next::prepare_clip_request(request), next::clipper_error);
    request = move_test_request();
    request.options.intersection_policy.mode = static_cast<next::precision_mode>(999);
    EXPECT_THROW((void)next::prepare_clip_request(request), next::clipper_error);
}

TEST(Clipper2NextPreparedClipRequestTests, PreparedClipUnionWithEmptySubjectsReturnsClipClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.clips = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto prepared = next::prepare_clip_request(request);
    const auto result = next::clip(prepared);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed[0].size(), 4U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextPreparedClipRequestTests, PreparedClipXorWithEmptySubjectsReturnsClipClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Xor;
    request.fill_rule = next::FillRule::NonZero;
    request.clips = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto prepared = next::prepare_clip_request(request);
    const auto result = next::clip(prepared);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed[0].size(), 4U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextPreparedClipRequestTests, PreparedOpenClipRequestMatchesUnpreparedExecution) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::EvenOdd;
    request.open_subjects = {{{0, 50}, {100, 50}}};
    request.clips = {{{25, 25}, {75, 25}, {75, 75}, {25, 75}}};

    const auto prepared = next::prepare_clip_request(request);
    const auto unprepared = next::clip(request);
    const auto prepared_first = next::clip(prepared);
    const auto prepared_second = next::clip(prepared);

    EXPECT_EQ(prepared_first.closed, unprepared.closed);
    EXPECT_EQ(prepared_first.open, unprepared.open);
    EXPECT_EQ(prepared_second.closed, unprepared.closed);
    EXPECT_EQ(prepared_second.open, unprepared.open);
}

TEST(Clipper2NextPreparedClipRequestTests, PreparedClipRequestCachesShapeMetadata) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({10, 10, 40, 10, 40, 40, 10, 40}),
    };
    request.open_subjects = next::Paths64{
        {{0, 0}, {10, 10}, {20, 0}},
    };
    request.clips = next::Paths64{
        test::path64({0, 0, 50, 0, 50, 50, 0, 50}),
    };

    const auto prepared = next::prepare_clip_request(request);

    const auto& metadata = prepared.metadata();
    EXPECT_EQ(metadata.subject_path_count, 1U);
    EXPECT_EQ(metadata.open_subject_path_count, 1U);
    EXPECT_EQ(metadata.clip_path_count, 1U);
    EXPECT_EQ(metadata.subject_point_count, 4U);
    EXPECT_EQ(metadata.open_subject_point_count, 3U);
    EXPECT_EQ(metadata.clip_point_count, 4U);
    ASSERT_TRUE(metadata.single_subject_rect);
    EXPECT_EQ(*metadata.single_subject_rect, (next::Rect64{10, 10, 40, 40}));
    ASSERT_TRUE(metadata.single_clip_rect);
    EXPECT_EQ(*metadata.single_clip_rect, (next::Rect64{0, 0, 50, 50}));
}

TEST(Clipper2NextPreparedClipRequestTests, PreparedClipRequestSnapshotsSourceRequest) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({10, 10, 40, 10, 40, 40, 10, 40}),
    };
    request.clips = next::Paths64{
        test::path64({0, 0, 50, 0, 50, 50, 0, 50}),
    };

    const auto prepared = next::prepare_clip_request(request);
    request.subjects.clear();
    request.clips.clear();

    const auto result = next::clip(prepared);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed.front().size(), 4U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextPreparedClipRequestTests, PreparedClipBatchMatchesUnpreparedBatch) {
    std::vector<next::clip_request64> requests;
    {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Intersection;
        request.fill_rule = next::FillRule::NonZero;
        request.subjects = next::Paths64{
            test::path64({10, 10, 40, 10, 40, 40, 10, 40}),
        };
        request.clips = next::Paths64{
            test::path64({0, 0, 50, 0, 50, 50, 0, 50}),
        };
        requests.push_back(std::move(request));
    }
    {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Union;
        request.fill_rule = next::FillRule::NonZero;
        request.subjects = next::Paths64{
            test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
        };
        requests.push_back(std::move(request));
    }

    std::vector<next::prepared_clip_request64> prepared_requests;
    prepared_requests.reserve(requests.size());
    for (const auto& request : requests) {
        prepared_requests.push_back(next::prepare_clip_request(request));
    }

    const auto unprepared = next::clip_batch(requests);
    const auto prepared = next::clip_batch(prepared_requests);

    ASSERT_EQ(prepared.size(), unprepared.size());
    for (std::size_t index = 0; index < prepared.size(); ++index) {
        EXPECT_EQ(prepared[index].closed, unprepared[index].closed);
        EXPECT_EQ(prepared[index].open, unprepared[index].open);
    }
}

TEST(Clipper2NextPreparedClipRequestTests, ConcurrentPreparedExecutionsMatchSingleThreadedResult) {
    // Exercises the documented invariant that a prepared request's shared
    // vertex storage and local-minima list are strictly read-only during
    // execute(): every thread reuses the same prepared_clip_request64 while
    // also touching its own thread-local engine state and caches. Run under
    // ThreadSanitizer (linux-gcc-tsan preset) this guards the concurrency
    // contract; elsewhere it still verifies result determinism.
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    // Concave subject so the rectangle fast paths cannot satisfy the request
    // and the full scanline engine runs on the shared prepared data.
    request.subjects = next::Paths64{
        {{0, 0}, {100, 0}, {100, 40}, {50, 40}, {50, 60}, {100, 60}, {100, 100}, {0, 100}},
    };
    request.clips = next::Paths64{
        {{25, -10}, {120, 30}, {80, 110}, {-10, 70}},
    };

    const auto prepared = next::prepare_clip_request(request);
    const auto baseline = next::clip(prepared);
    ASSERT_FALSE(baseline.closed.empty());

    constexpr std::size_t thread_count = 8U;
    constexpr std::size_t iterations_per_thread = 25U;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    std::array<std::atomic<bool>, thread_count> matched{};
    for (auto& flag : matched) { flag.store(true); }

    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back([&, thread_index] {
            for (std::size_t iteration = 0; iteration < iterations_per_thread; ++iteration) {
                const auto result = next::clip(prepared);
                if (result.closed != baseline.closed || result.open != baseline.open) {
                    matched[thread_index].store(false);
                }
                // Releasing this thread's caches mid-run must not disturb
                // other threads or subsequent executions.
                if (iteration == iterations_per_thread / 2U) {
                    next::release_thread_caches();
                }
            }
        });
    }
    for (auto& worker : workers) { worker.join(); }

    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        EXPECT_TRUE(matched[thread_index].load()) << "thread " << thread_index;
    }
}

TEST(Clipper2NextPreparedClipRequestTests, RectangleMetadataPreservesCyclicOrderAndWinding) {
    for (const auto extent : {std::int64_t{1}, std::int64_t{1} << 30, std::int64_t{1} << 60}) {
        const auto expected = next::Rect64{-extent, -extent + 1, extent + 1, extent + 2};
        const auto original = next::Path64{{expected.left, expected.top}, {expected.right, expected.top},
                                          {expected.right, expected.bottom}, {expected.left, expected.bottom}};
        for (std::size_t shift = 0; shift < 4U; ++shift) {
            for (const bool reverse : {false, true}) {
                auto ring = original;
                std::rotate(ring.begin(), ring.begin() + shift, ring.end());
                if (reverse) { std::reverse(ring.begin(), ring.end()); }
                next::clip_request64 request;
                request.subjects = {ring};
                request.clips = {ring};
                const auto prepared = next::prepare_clip_request(request);
                EXPECT_EQ(prepared.metadata().single_subject_rect, std::optional<next::Rect64>{expected});
                EXPECT_EQ(prepared.metadata().single_clip_rect, std::optional<next::Rect64>{expected});
                EXPECT_EQ(prepared.metadata().subject_point_count, 4U);
                EXPECT_EQ(prepared.metadata().clip_point_count, 4U);
                EXPECT_EQ(prepared.request().subjects, request.subjects);
                EXPECT_EQ(prepared.request().clips, request.clips);
            }
        }
    }
}

TEST(Clipper2NextPreparedClipRequestTests, RectangleMetadataRejectsDiagonalDuplicateAndDegenerateRings) {
    const auto rings = next::Paths64{
        {}, {{0, 0}}, {{0, 0}, {3, 0}}, {{0, 0}, {3, 0}, {3, 4}},
        {{0, 0}, {3, 4}, {3, 0}, {0, 4}},
        {{0, 0}, {3, 0}, {3, 0}, {0, 4}},
        {{0, 0}, {3, 0}, {3, 0}, {0, 0}},
        {{0, 0}, {0, 4}, {0, 4}, {0, 0}},
        {{0, 0}, {3, 0}, {2, 4}, {0, 4}},
        {{0, 0}, {3, 0}, {3, 4}, {0, 4}, {0, 0}},
    };
    for (const auto& ring : rings) {
        next::clip_request64 request;
        request.subjects = {ring};
        request.clips = {ring};
        const auto prepared = next::prepare_clip_request(request);
        EXPECT_FALSE(prepared.metadata().single_subject_rect);
        EXPECT_FALSE(prepared.metadata().single_clip_rect);
        EXPECT_EQ(prepared.metadata().subject_point_count, ring.size());
        EXPECT_EQ(prepared.metadata().clip_point_count, ring.size());
    }
}
