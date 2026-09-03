#include "clipper2next/offset.h"
#include "clipper2next/api/execution.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cfenv>
#include <limits>
#include <thread>
#include <vector>

namespace next = clipper2next;

namespace {

struct foreign_point final {
    std::int64_t x{};
    std::int64_t y{};
};

using foreign_path = std::vector<foreign_point>;
using foreign_paths = std::vector<foreign_path>;

[[nodiscard]] auto rectangle(std::int64_t left,
                             std::int64_t top,
                             std::int64_t right,
                             std::int64_t bottom) -> foreign_path {
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

[[nodiscard]] auto materialize(const next::path_set64& source) -> next::Paths64 {
    auto result = next::Paths64{};
    result.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto path = source[index];
        result.emplace_back(path.begin(), path.end());
    }
    return result;
}

[[nodiscard]] auto dense_rectangle(
    const std::int64_t left,
    const std::int64_t top,
    const std::int64_t edge) -> foreign_path {
    auto path = foreign_path{};
    path.reserve(static_cast<std::size_t>(edge) * 4U);
    for (auto value = std::int64_t{}; value <= edge; ++value) {
        path.push_back({left + value, top});
    }
    for (auto value = std::int64_t{1}; value <= edge; ++value) {
        path.push_back({left + edge, top + value});
    }
    for (auto value = edge - 1; value >= 0; --value) {
        path.push_back({left + value, top + edge});
    }
    for (auto value = edge - 1; value > 0; --value) {
        path.push_back({left, top + value});
    }
    return path;
}

struct recording_executor final {
    std::size_t invocation_count{};
    std::size_t item_count{};
    std::size_t requested_concurrency{};
    next::bulk_execution_error result{next::bulk_execution_error::none};
};

[[nodiscard]] auto execute_recorded_chunks(
    void* const context,
    const std::size_t item_count,
    const std::size_t minimum_grain,
    const std::size_t requested_concurrency,
    const next::bulk_task_ref task) noexcept -> next::bulk_execution_error {
    auto& executor = *static_cast<recording_executor*>(context);
    if (executor.result != next::bulk_execution_error::none) {
        return executor.result;
    }
    executor.item_count = item_count;
    executor.requested_concurrency = requested_concurrency;
    const auto grain = std::max<std::size_t>(minimum_grain, 1U);
    for (auto begin = std::size_t{}; begin < item_count; begin += grain) {
        ++executor.invocation_count;
        task(begin, std::min(item_count, begin + grain));
    }
    return next::bulk_execution_error::none;
}

[[nodiscard]] auto parallel_eligible_source() -> foreign_paths {
    auto source = foreign_paths{};
    source.reserve(512U);
    for (auto index = std::int64_t{}; index < 512; ++index) {
        source.push_back(dense_rectangle(index * 2'000, 0, 256));
    }
    return source;
}

[[nodiscard]] auto subthreshold_source() -> foreign_paths {
    auto source = foreign_paths{};
    source.reserve(64U);
    for (auto index = std::int64_t{}; index < 64; ++index) {
        source.push_back(dense_rectangle(index * 1'000, 0, 64));
    }
    return source;
}

struct threaded_executor final {
    std::size_t worker_count{};
    std::atomic_size_t invocation_count{};
};

[[nodiscard]] auto execute_threaded_chunks(
    void* const context,
    const std::size_t item_count,
    const std::size_t minimum_grain,
    const std::size_t requested_concurrency,
    const next::bulk_task_ref task) noexcept -> next::bulk_execution_error {
    auto& executor = *static_cast<threaded_executor*>(context);
    const auto grain = std::max<std::size_t>(minimum_grain, 1U);
    auto next_begin = std::atomic_size_t{};
    const auto worker = [&] {
        while (true) {
            const auto begin = next_begin.fetch_add(
                grain, std::memory_order_relaxed);
            if (begin >= item_count) {
                return;
            }
            executor.invocation_count.fetch_add(
                1U, std::memory_order_relaxed);
            task(begin, std::min(item_count, begin + grain));
        }
    };
    const auto active_workers = std::min(
        executor.worker_count, requested_concurrency);
    try {
        auto workers = std::vector<std::jthread>{};
        workers.reserve(active_workers - 1U);
        for (auto index = std::size_t{1U};
             index < active_workers;
             ++index) {
            workers.emplace_back(worker);
        }
        worker();
    } catch (const std::bad_alloc&) {
        return next::bulk_execution_error::allocation_failure;
    } catch (...) {
        return next::bulk_execution_error::scheduler_failure;
    }
    return next::bulk_execution_error::none;
}

}  // namespace

TEST(Clipper2NextBorrowedOffsetApiTests,
     ForeignRangesReachTheOffsetKernelWithoutAnOwningInputCollection) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->paths.size(), 1U);
    EXPECT_EQ(result->paths.point_count(), 4U);
    EXPECT_EQ(result->stats.input_path_count, 1U);
    EXPECT_EQ(result->stats.input_point_count, 4U);
    EXPECT_EQ(result->stats.input_collection_point_writes, 0U);
    EXPECT_EQ(result->stats.engine_input_point_writes, 4U);
    EXPECT_EQ(result->stats.output_path_count, 1U);
    EXPECT_EQ(result->stats.output_point_count, 4U);
    EXPECT_GT(result->stats.peak_workspace_bytes, 0U);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     FlatInputStagingDoesNotAllocateOneOwnerPerBorrowedPath) {
    auto source = foreign_paths{};
    source.reserve(128U);
    for (std::int64_t index = 0; index < 128; ++index) {
        const auto left = index * 1000;
        source.push_back(rectangle(left, 0, left + 100, 100));
    }
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->stats.input_path_count, source.size());
    EXPECT_EQ(result->stats.input_collection_point_writes, 0U);
    EXPECT_EQ(result->stats.engine_input_point_writes, source.size() * 4U);
    EXPECT_LE(result->stats.staging_reallocation_count, 3U);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     BorrowedAndOwningRequestsHaveIdenticalCanonicalOutput) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto borrowed = next::borrowed_offset_request64{};
    borrowed.paths = next::borrow_paths64(source);
    borrowed.delta = 7.0;
    borrowed.join_type = next::JoinType::Round;
    borrowed.arc_tolerance = 0.25;

    auto owning = next::offset_request64{};
    owning.paths = next::Paths64{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };
    owning.delta = borrowed.delta;
    owning.join_type = borrowed.join_type;
    owning.arc_tolerance = borrowed.arc_tolerance;

    const auto borrowed_result = next::offset_stage_checked(borrowed);
    const auto owning_result = next::offset_checked(owning);

    ASSERT_TRUE(borrowed_result.has_value());
    ASSERT_TRUE(owning_result.has_value());
    EXPECT_EQ(materialize(borrowed_result->paths), owning_result->closed);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     InputOutputAndWorkspaceLimitsFailClosed) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;

    request.limits.maximum_input_point_count = 3U;
    auto result = next::offset_stage_checked(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::resource_limit);

    request.limits = {};
    request.limits.maximum_output_point_count = 3U;
    result = next::offset_stage_checked(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::resource_limit);

    request.limits = {};
    request.limits.maximum_staging_workspace_bytes = 1U;
    result = next::offset_stage_checked(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::resource_limit);

    request.limits = {};
    request.limits.maximum_engine_work = 0U;
    result = next::offset_stage_checked(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::resource_limit);

    request.limits = {};
    request.limits.maximum_engine_workspace_bytes = 0U;
    result = next::offset_stage_checked(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::resource_limit);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ZeroDeltaStillUsesTheBorrowedSingleCopyContract) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->paths.size(), 1U);
    EXPECT_EQ(result->stats.input_collection_point_writes, 0U);
    EXPECT_EQ(result->stats.engine_input_point_writes, 4U);
    EXPECT_EQ(result->stats.output_point_count, 4U);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ZeroDeltaRetainsOpenPathSemanticsInTheSharedDescriptor) {
    const auto source = foreign_paths{{{0, 0}, {100, 0}}};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.end_type = next::EndType::Butt;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->paths.size(), 1U);
    EXPECT_EQ(result->paths[0].closure, geotypes::PathClosure::Open);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     CoordinatesOutsideTheOffsetKernelRangeFailExplicitly) {
    const auto source = foreign_paths{{
        {(std::numeric_limits<std::int64_t>::max)(), 0},
        {0, 0},
        {0, 1},
    }};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 1.0;

    const auto result = next::offset_stage_checked(request);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     OffsetExecutionIsIndependentOfAndRestoresTheCallingRoundingMode) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 2.5;
    request.join_type = next::JoinType::Miter;
    request.arc_tolerance = 0.25;

    const auto original_mode = std::fegetround();
    ASSERT_NE(original_mode, -1);
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const auto reference = next::offset_stage_checked(request);
    ASSERT_TRUE(reference.has_value());

    for (const auto mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        const auto result = next::offset_stage_checked(request);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::fegetround(), mode);
        EXPECT_EQ(materialize(result->paths), materialize(reference->paths));
    }
    ASSERT_EQ(std::fesetround(original_mode), 0);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ExplicitAwayFromZeroRoundingPreservesHalfUnitOffsets) {
    const auto source = foreign_paths{rectangle(0, 0, 4, 4)};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 0.5;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;
    request.coordinate_rounding =
        geotypes::CoordinateRounding::NearestAwayFromZero;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->paths.size(), 1U);
    ASSERT_EQ(result->paths[0].points.size(), 4U);
    auto minimum_x = result->paths[0].points.front().x;
    auto minimum_y = result->paths[0].points.front().y;
    auto maximum_x = minimum_x;
    auto maximum_y = minimum_y;
    for (const auto point : result->paths[0].points) {
        minimum_x = std::min(minimum_x, point.x);
        minimum_y = std::min(minimum_y, point.y);
        maximum_x = std::max(maximum_x, point.x);
        maximum_y = std::max(maximum_y, point.y);
    }
    EXPECT_EQ(minimum_x, -1);
    EXPECT_EQ(minimum_y, -1);
    EXPECT_EQ(maximum_x, 5);
    EXPECT_EQ(maximum_y, 5);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ExplicitQuadrantSegmentsOwnRoundJoinResolution) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 20.0;
    request.join_type = next::JoinType::Round;
    request.end_type = next::EndType::Polygon;
    request.arc_segments_per_quadrant = 1U;

    const auto coarse = next::offset_stage_checked(request);
    ASSERT_TRUE(coarse.has_value());

    request.arc_segments_per_quadrant = 8U;
    const auto fine = next::offset_stage_checked(request);
    ASSERT_TRUE(fine.has_value());

    EXPECT_GT(fine->paths.point_count(), coarse->paths.point_count());
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ExplicitExecutorPreservesSerialResultAndInputOrder) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial.has_value());
    auto executor_state = recording_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 16U, &execute_recorded_chunks};

    const auto concurrent = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(concurrent.has_value());
    EXPECT_GT(executor_state.invocation_count, 0U);
    EXPECT_EQ(materialize(concurrent->paths), materialize(serial->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ExecutorCapabilityAboveKernelMaximumUsesOneEffectiveConcurrency) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;
    auto reference_state = recording_executor{};
    const auto reference_executor = next::sync_bulk_executor_ref{
        &reference_state, 16U, &execute_recorded_chunks};
    const auto reference = next::offset_stage_checked(
        request, reference_executor);
    ASSERT_TRUE(reference.has_value());

    auto wide_state = recording_executor{};
    const auto wide_executor = next::sync_bulk_executor_ref{
        &wide_state, 1'000U, &execute_recorded_chunks};
    const auto result = next::offset_stage_checked(request, wide_executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(wide_state.requested_concurrency, 16U);
    EXPECT_EQ(wide_state.item_count, reference_state.item_count);
    EXPECT_EQ(wide_state.item_count, 64U);
    EXPECT_EQ(
        result->stats.planned_engine_workspace_bytes,
        reference->stats.planned_engine_workspace_bytes);
    EXPECT_EQ(materialize(result->paths), materialize(reference->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ExecutorFailureIsNotReportedAsGeometryFailure) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    auto executor_state = recording_executor{
        .result = next::bulk_execution_error::scheduler_failure};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::executor_failure);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ParallelWorkspaceSharesTheDirectPreparationPeak) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial.has_value());
    request.limits.maximum_engine_workspace_bytes =
        serial->stats.planned_engine_workspace_bytes;
    auto executor_state = recording_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_GT(executor_state.invocation_count, 0U);
    EXPECT_EQ(
        result->stats.planned_engine_workspace_bytes,
        serial->stats.planned_engine_workspace_bytes);
    EXPECT_EQ(materialize(result->paths), materialize(serial->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ActualCleanupSizeAdmitsDenseDisjointParallelOffset) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;
    const auto reference = next::offset_stage_checked(request);
    ASSERT_TRUE(reference.has_value());
    request.limits.maximum_engine_work = 2'147'483'648ULL;
    request.limits.maximum_engine_workspace_bytes =
        128ULL * 1024ULL * 1024ULL * 1024ULL;
    auto executor_state = recording_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value())
        << "error=" << static_cast<int>(result.error())
        << " reference_points=" << reference->stats.output_point_count;
    EXPECT_TRUE(result->stats.output_is_disjoint_simple_shells);
    EXPECT_GT(executor_state.invocation_count, 0U);
    EXPECT_LE(
        result->stats.planned_engine_work,
        request.limits.maximum_engine_work);
    EXPECT_LE(
        result->stats.planned_engine_workspace_bytes,
        request.limits.maximum_engine_workspace_bytes);
    EXPECT_EQ(materialize(result->paths), materialize(reference->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     NegativeOffsetDoesNotClaimDisjointShellTopology) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = -5.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->stats.output_is_disjoint_simple_shells);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ConcurrentExecutorIsByteExactWithSerialOffset) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial.has_value());
    auto executor_state = threaded_executor{.worker_count = 16U};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, executor_state.worker_count,
        &execute_threaded_chunks};

    const auto concurrent = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(concurrent.has_value());
    EXPECT_GT(
        executor_state.invocation_count.load(std::memory_order_relaxed), 1U);
    EXPECT_EQ(materialize(concurrent->paths), materialize(serial->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     SubthresholdOffsetRemainsSerialWithAnExecutor) {
    const auto source = subthreshold_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    auto executor_state = recording_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(executor_state.invocation_count, 0U);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     InsufficientConcurrencyRemainsSerial) {
    const auto source = parallel_eligible_source();
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    auto executor_state = recording_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 8U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(executor_state.invocation_count, 0U);
}
