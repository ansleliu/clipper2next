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
