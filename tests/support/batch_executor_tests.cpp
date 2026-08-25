#include <gtest/gtest.h>

#include "batch/private/batch_clip_executor.h"
#include "clip/private/clip_request_metadata.h"

#include <memory_resource>

namespace next = clipper2next;

TEST(Clipper2NextBatchExecutorTests, ClipBatchWorkOrderRunsHeavyFullEngineItemsFirst) {
    const std::vector<next::internal::clip_batch_work_profile> profiles{
        {.input_index = 0U, .estimated_point_count = 12U, .likely_fast_path = true},
        {.input_index = 1U, .estimated_point_count = 500U, .likely_fast_path = false},
        {.input_index = 2U, .estimated_point_count = 20U, .likely_fast_path = false},
        {.input_index = 3U, .estimated_point_count = 300U, .likely_fast_path = true},
    };

    const auto order = next::internal::build_clip_batch_work_order(profiles);

    EXPECT_EQ(order, (std::vector<std::size_t>{1U, 2U, 3U, 0U}));
}

TEST(Clipper2NextBatchExecutorTests, ClipBatchWorkProfilesRecognizeUniformInputOrder) {
    const std::vector<next::internal::clip_batch_work_profile> profiles{
        {.input_index = 0U, .estimated_point_count = 8U, .likely_fast_path = true},
        {.input_index = 1U, .estimated_point_count = 8U, .likely_fast_path = true},
        {.input_index = 2U, .estimated_point_count = 8U, .likely_fast_path = true},
    };

    EXPECT_TRUE(next::internal::clip_batch_work_profiles_are_uniform(profiles));
}

TEST(Clipper2NextBatchExecutorTests, ClipBatchWorkProfilesRejectMixedCostInputOrder) {
    const std::vector<next::internal::clip_batch_work_profile> profiles{
        {.input_index = 0U, .estimated_point_count = 8U, .likely_fast_path = true},
        {.input_index = 1U, .estimated_point_count = 500U, .likely_fast_path = false},
        {.input_index = 2U, .estimated_point_count = 8U, .likely_fast_path = true},
    };

    EXPECT_FALSE(next::internal::clip_batch_work_profiles_are_uniform(profiles));
}

TEST(Clipper2NextBatchExecutorTests, ClipBatchRequestsRecognizeUniformShape) {
    next::clip_request64 first;
    first.clip_type = next::ClipType::Union;
    first.fill_rule = next::FillRule::NonZero;
    first.subjects = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    first.clips = {{{5, 5}, {15, 5}, {15, 15}, {5, 15}}};
    auto second = first;

    const std::vector<next::clip_request64> requests{first, second};

    EXPECT_TRUE(next::internal::clip_batch_requests_have_uniform_shape(requests));
}

TEST(Clipper2NextBatchExecutorTests, ClipBatchRequestsRejectMixedShape) {
    next::clip_request64 first;
    first.clip_type = next::ClipType::Union;
    first.fill_rule = next::FillRule::NonZero;
    first.subjects = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    first.clips = {{{5, 5}, {15, 5}, {15, 15}, {5, 15}}};
    auto second = first;
    second.subjects.front().push_back({0, 5});

    const std::vector<next::clip_request64> requests{first, second};

    EXPECT_FALSE(next::internal::clip_batch_requests_have_uniform_shape(requests));
}

TEST(Clipper2NextBatchExecutorTests,
     ClipBatchWorkProfileRecognizesContainedRectIntersectionCandidate) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{10, 10}, {90, 10}, {90, 90}, {10, 90}}};
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    const auto metadata = next::internal::build_clip_request_metadata(request);

    const auto profile = next::internal::build_clip_batch_work_profile(7U, request, metadata);

    EXPECT_EQ(profile.input_index, 7U);
    EXPECT_EQ(profile.estimated_point_count, 8U);
    EXPECT_TRUE(profile.likely_fast_path);
}

TEST(Clipper2NextBatchExecutorTests, ClipBatchWorkProfileRejectsNonDefaultOptions) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.options.reverse_solution = true;
    request.subjects = {{{10, 10}, {90, 10}, {90, 90}, {10, 90}}};
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    const auto metadata = next::internal::build_clip_request_metadata(request);

    const auto profile = next::internal::build_clip_batch_work_profile(0U, request, metadata);

    EXPECT_FALSE(profile.likely_fast_path);
}

TEST(Clipper2NextBatchExecutorTests, BatchLargeThresholdIsExplicit) {
    EXPECT_EQ(next::internal::clip_batch_parallel_threshold(), 1024U);
}

TEST(Clipper2NextBatchExecutorTests, ParallelSafetyAcceptsRequestsWithoutCallbacks) {
    std::vector<next::clip_request64> requests(2);
    EXPECT_TRUE(next::internal::clip_batch_requests_are_parallel_safe(requests));
}
