#include "clipper2next/batch.h"
#include "clipper2next/clipper.h"
#include "clipper2next/api/execution.h"
#include "batch/private/batch_clip_executor.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace next = clipper2next;
namespace test = clipper2next::tests;

namespace {

auto make_clip_request(next::ClipType clip_type,
                       next::Paths64 subjects,
                       next::Paths64 clips = {},
                       next::Paths64 open_subjects = {}) -> next::clip_request64 {
    next::clip_request64 request;
    request.clip_type = clip_type;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = std::move(subjects);
    request.clips = std::move(clips);
    request.open_subjects = std::move(open_subjects);
    return request;
}

auto generated_rect(int64_t left, int64_t top, int64_t size) -> next::Path64 {
    return test::path64({
        left,
        top,
        left + size,
        top,
        left + size,
        top + size,
        left,
        top + size,
    });
}

auto make_generated_batch_request(int index) -> next::clip_request64 {
    const auto base_x = static_cast<int64_t>((index % 16) * 25);
    const auto base_y = static_cast<int64_t>((index / 16) * 25);
    const auto subject = next::Paths64{generated_rect(base_x, base_y, 80 + (index % 5) * 7)};
    const auto clip =
        next::Paths64{generated_rect(base_x + 20, base_y + 10, 75 + (index % 3) * 11)};
    if (index % 7 == 0) {
        const auto open_lines = next::Paths64{
            test::path64(
                {base_x - 25, base_y + 35, base_x + 50, base_y + 35, base_x + 140, base_y + 35}),
        };
        return make_clip_request(next::ClipType::Intersection, {}, clip, open_lines);
    }
    switch (index % 4) {
    case 0: {
        return make_clip_request(next::ClipType::Intersection, subject, clip);
    }
    case 1: {
        return make_clip_request(next::ClipType::Union, subject, clip);
    }
    case 2: {
        return make_clip_request(next::ClipType::Difference, subject, clip);
    }
    default: {
        return make_clip_request(next::ClipType::Xor, subject, clip);
    }
    }
}

struct batch_recording_executor final {
    std::size_t invocation_count{};
    next::bulk_execution_error result{next::bulk_execution_error::none};
};

[[nodiscard]] auto execute_batch_chunks(
    void* const context,
    const std::size_t item_count,
    const std::size_t minimum_grain,
    const std::size_t requested_concurrency,
    const next::bulk_task_ref task) noexcept -> next::bulk_execution_error {
    static_cast<void>(requested_concurrency);
    auto& executor = *static_cast<batch_recording_executor*>(context);
    if (executor.result != next::bulk_execution_error::none) {
        return executor.result;
    }
    const auto grain = std::max<std::size_t>(minimum_grain, 1U);
    for (auto begin = std::size_t{}; begin < item_count; begin += grain) {
        ++executor.invocation_count;
        task(begin, std::min(item_count, begin + grain));
    }
    return next::bulk_execution_error::none;
}

}  // namespace

TEST(Clipper2NextDeterminismTests, PublicBatchClipPreservesRequestOrder) {
    std::vector<next::clip_request64> requests(2);
    requests[0].clip_type = next::ClipType::Union;
    requests[0].fill_rule = next::FillRule::NonZero;
    requests[0].subjects = {test::path64({0, 0, 10, 0, 10, 10, 0, 10})};
    requests[1].clip_type = next::ClipType::Union;
    requests[1].fill_rule = next::FillRule::NonZero;
    requests[1].subjects = {test::path64({20, 20, 30, 20, 30, 30, 20, 30})};

    const auto results = next::clip_batch(requests);

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].closed.size(), 1U);
    EXPECT_EQ(results[1].closed.size(), 1U);
}

TEST(Clipper2NextDeterminismTests, PublicBatchClipMatchesIndividualMixedOperations) {
    const auto a = next::Paths64{test::path64({0, 0, 100, 0, 100, 100, 0, 100})};
    const auto b = next::Paths64{test::path64({50, 50, 150, 50, 150, 150, 50, 150})};
    const auto c = next::Paths64{test::path64({200, 0, 260, 0, 260, 60, 200, 60})};
    const auto clip_rect = next::Paths64{test::path64({25, 25, 125, 25, 125, 125, 25, 125})};
    const auto open_lines = next::Paths64{test::path64({-20, 50, 40, 50, 140, 50})};

    const auto requests = std::vector<next::clip_request64>{
        make_clip_request(next::ClipType::Intersection, a, clip_rect),
        make_clip_request(next::ClipType::Union, a, b),
        make_clip_request(next::ClipType::Difference, a, b),
        make_clip_request(next::ClipType::Xor, a, b),
        make_clip_request(next::ClipType::Intersection, {}, clip_rect, open_lines),
        make_clip_request(next::ClipType::Union, c)};

    const auto batch_results = next::clip_batch(requests);

    ASSERT_EQ(batch_results.size(), requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        SCOPED_TRACE(index);
        const auto individual = next::clip(requests[index]);
        EXPECT_EQ(batch_results[index].closed, individual.closed);
        EXPECT_EQ(batch_results[index].open, individual.open);
    }
}

TEST(Clipper2NextDeterminismTests, PreparedBatchClipMatchesUnpreparedMixedOperations) {
    const auto requests = std::vector<next::clip_request64>{
        make_clip_request(next::ClipType::Intersection,
                          next::Paths64{test::path64({0, 0, 90, 0, 90, 90, 0, 90})},
                          next::Paths64{test::path64({30, 30, 120, 30, 120, 120, 30, 120})}),
        make_clip_request(next::ClipType::Xor,
                          next::Paths64{test::path64({200, 0, 280, 0, 280, 80, 200, 80})},
                          next::Paths64{test::path64({240, 40, 320, 40, 320, 120, 240, 120})})};

    std::vector<next::prepared_clip_request64> prepared_requests;
    prepared_requests.reserve(requests.size());
    for (const auto& request : requests) {
        prepared_requests.push_back(next::prepare_clip_request(request));
    }

    const auto unprepared_results = next::clip_batch(requests);
    const auto prepared_results = next::clip_batch(prepared_requests);

    ASSERT_EQ(prepared_results.size(), unprepared_results.size());
    for (std::size_t index = 0; index < prepared_results.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(prepared_results[index].closed, unprepared_results[index].closed);
        EXPECT_EQ(prepared_results[index].open, unprepared_results[index].open);
    }
}

TEST(Clipper2NextDeterminismTests, LargeMixedBatchMatchesIndividualAndPreparedExecution) {
    const auto request_count = next::internal::clip_batch_parallel_threshold() + 17U;
    std::vector<next::clip_request64> requests;
    requests.reserve(request_count);
    for (std::size_t index = 0; index < request_count; ++index) {
        requests.push_back(make_generated_batch_request(static_cast<int>(index)));
    }

    const auto batch_results = next::clip_batch(requests);
    std::vector<next::prepared_clip_request64> prepared_requests;
    prepared_requests.reserve(requests.size());
    for (const auto& request : requests) {
        prepared_requests.push_back(next::prepare_clip_request(request));
    }
    const auto prepared_results = next::clip_batch(prepared_requests);

    ASSERT_EQ(batch_results.size(), requests.size());
    ASSERT_EQ(prepared_results.size(), requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        SCOPED_TRACE(index);
        const auto individual = next::clip(requests[index]);
        EXPECT_EQ(batch_results[index].closed, individual.closed);
        EXPECT_EQ(batch_results[index].open, individual.open);
        EXPECT_EQ(prepared_results[index].closed, individual.closed);
        EXPECT_EQ(prepared_results[index].open, individual.open);
    }
}

TEST(Clipper2NextDeterminismTests,
     ExplicitExecutorPreservesLargeMixedBatchResultsAndOrder) {
    const auto request_count = next::internal::clip_batch_parallel_threshold() + 17U;
    auto requests = std::vector<next::clip_request64>{};
    requests.reserve(request_count);
    for (auto index = std::size_t{}; index < request_count; ++index) {
        requests.push_back(make_generated_batch_request(static_cast<int>(index)));
    }
    const auto serial = next::clip_batch(requests);
    auto executor_state = batch_recording_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 4U, &execute_batch_chunks};

    const auto concurrent = next::clip_batch_checked(requests, executor);

    ASSERT_TRUE(concurrent.has_value());
    EXPECT_GT(executor_state.invocation_count, 0U);
    ASSERT_EQ(concurrent->size(), serial.size());
    for (auto index = std::size_t{}; index < serial.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ((*concurrent)[index].closed, serial[index].closed);
        EXPECT_EQ((*concurrent)[index].open, serial[index].open);
    }
}

TEST(Clipper2NextDeterminismTests,
     CheckedBatchReportsExecutorFailureSeparately) {
    const auto request_count = next::internal::clip_batch_parallel_threshold();
    auto requests = std::vector<next::clip_request64>{};
    requests.reserve(request_count);
    for (auto index = std::size_t{}; index < request_count; ++index) {
        requests.push_back(make_generated_batch_request(static_cast<int>(index)));
    }
    auto executor_state = batch_recording_executor{
        .result = next::bulk_execution_error::scheduler_failure};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 4U, &execute_batch_chunks};

    const auto result = next::clip_batch_checked(requests, executor);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::executor_failure);
}
