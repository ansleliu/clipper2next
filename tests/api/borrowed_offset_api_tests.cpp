#include "clipper2next/offset.h"
#include "clipper2next/geometry.h"
#include "clipper2next/api/execution.h"
#include "clipper2next/api/memory.h"
#include "geometry/private/path_simplicity.h"
#include "support/path_equivalence.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cfenv>
#include <limits>
#include <numbers>
#include <thread>
#include <functional>
#include <stdexcept>
#include "offset/private/offset_thread_state.h"
#include <vector>

namespace next = clipper2next;

TEST(Clipper2NextBorrowedOffsetApiTests, CleanupHonorsTheRequestedIntersectionPrecision) {
    const auto paths = next::Paths64{{{0, 0}, {12, 3}}, {{0, 4}, {12, 0}}};
    const auto group = next::borrowed_offset_group64{
        next::borrow_paths64(paths), next::JoinType::Miter, next::EndType::Butt};
    auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 1.0};
    request.options.intersection_policy.mode = next::precision_mode::precise;
    const auto result = next::offset_stage_checked(request);
    ASSERT_TRUE(result);
    // Rounded offset support lines are y=x/4-1 and y=3-x/3.
    // Their exact intersection is (48/7,5/7), nearest-even -> (7,1).
    const auto points = result->paths.points();
    EXPECT_NE(std::find(points.begin(), points.end(), next::Point64{7, 1}), points.end());
}

TEST(Clipper2NextBorrowedOffsetApiTests, OpenViewsAndRepeatedEndpointsMatchOwningOffset) {
    const auto inputs = std::vector<next::Paths64>{
        next::Paths64{next::Path64{{0, 0}, {20, 0}}},
        next::Paths64{next::Path64{{0, 0}, {20, 0}, {20, 20}, {0, 0}}},
        next::Paths64{next::Path64{{0, 0}, {0, 0}, {20, 0}, {20, 20}, {0, 0}, {0, 0}}}};
    for (const auto& paths : inputs) {
        auto flat = next::path_set64{};
        for (const auto& path : paths) { flat.append(path, geotypes::PathClosure::Open); }
        for (const auto end : {next::EndType::Butt, next::EndType::Square, next::EndType::Round}) {
            auto owned = next::offset_request64{};
            owned.paths = paths;
            owned.delta = 2.0;
            owned.join_type = next::JoinType::Round;
            owned.end_type = end;
            const auto expected = next::offset_checked(owned);
            ASSERT_TRUE(expected);
            for (const auto& source :
                 {next::borrow_paths64(flat.view()), next::borrow_paths64(paths)}) {
                auto borrowed_group = next::borrowed_offset_group64{};
                auto borrowed = next::borrowed_offset_request64{};
                borrowed.groups = std::span{&borrowed_group, 1U};
                borrowed_group.paths = source;
                borrowed.delta = owned.delta;
                borrowed_group.join_type = owned.join_type;
                borrowed_group.end_type = end;
                const auto actual = next::offset_stage_checked(borrowed);
                ASSERT_TRUE(actual);
                auto materialized = next::Paths64{};
                for (const auto path : actual->paths) {
                    materialized.emplace_back(path.begin(), path.end());
                }
                // Ring origins are not stable API identities. Coordinates,
                // orientation and vertex multiplicity remain exact.
                EXPECT_EQ(next::tests::oracle::canonical_closed_paths(std::move(materialized)),
                          next::tests::oracle::canonical_closed_paths(expected->closed));
            }
        }
    }
}

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

[[nodiscard]] auto dense_rectangle(const std::int64_t left,
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
    for (auto value = edge - 1; value >= 0; --value) { path.push_back({left + value, top + edge}); }
    for (auto value = edge - 1; value > 0; --value) { path.push_back({left, top + value}); }
    return path;
}

struct recording_executor final {
    std::size_t invocation_count{};
    std::size_t item_count{};
    std::size_t requested_concurrency{};
    next::bulk_execution_error result{next::bulk_execution_error::none};
};

[[nodiscard]] auto execute_recorded_chunks(void* const context,
                                           const std::size_t item_count,
                                           const std::size_t minimum_grain,
                                           const std::size_t requested_concurrency,
                                           const next::bulk_task_ref task) noexcept
    -> next::bulk_execution_error {
    auto& executor = *static_cast<recording_executor*>(context);
    if (executor.result != next::bulk_execution_error::none) { return executor.result; }
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

[[nodiscard]] auto execute_threaded_chunks(void* const context,
                                           const std::size_t item_count,
                                           const std::size_t minimum_grain,
                                           const std::size_t requested_concurrency,
                                           const next::bulk_task_ref task) noexcept
    -> next::bulk_execution_error {
    auto& executor = *static_cast<threaded_executor*>(context);
    const auto grain = std::max<std::size_t>(minimum_grain, 1U);
    auto next_begin = std::atomic_size_t{};
    const auto worker = [&] {
        while (true) {
            const auto begin = next_begin.fetch_add(grain, std::memory_order_relaxed);
            if (begin >= item_count) { return; }
            executor.invocation_count.fetch_add(1U, std::memory_order_relaxed);
            task(begin, std::min(item_count, begin + grain));
        }
    };
    const auto active_workers = std::min(executor.worker_count, requested_concurrency);
    try {
        auto workers = std::vector<std::jthread>{};
        workers.reserve(active_workers - 1U);
        for (auto index = std::size_t{1U}; index < active_workers; ++index) {
            workers.emplace_back(worker);
        }
        worker();
    } catch (const std::bad_alloc&) {
        return next::bulk_execution_error::allocation_failure;
    } catch (...) { return next::bulk_execution_error::scheduler_failure; }
    return next::bulk_execution_error::none;
}

}  // namespace

TEST(Clipper2NextBorrowedOffsetApiTests,
     ForeignRangesReachTheOffsetKernelWithoutAnOwningInputCollection) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;

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

TEST(Clipper2NextBorrowedOffsetApiTests, FlatInputStagingDoesNotAllocateOneOwnerPerBorrowedPath) {
    auto source = foreign_paths{};
    source.reserve(128U);
    for (std::int64_t index = 0; index < 128; ++index) {
        const auto left = index * 1000;
        source.push_back(rectangle(left, 0, left + 100, 100));
    }
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->stats.input_path_count, source.size());
    EXPECT_EQ(result->stats.input_collection_point_writes, 0U);
    EXPECT_EQ(result->stats.engine_input_point_writes, source.size() * 4U);
    EXPECT_LE(result->stats.staging_reallocation_count, 3U);
}

TEST(Clipper2NextBorrowedOffsetApiTests, BorrowedAndOwningRequestsHaveIdenticalCanonicalOutput) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto borrowed_group = next::borrowed_offset_group64{};
    auto borrowed = next::borrowed_offset_request64{};
    borrowed.groups = std::span{&borrowed_group, 1U};
    borrowed_group.paths = next::borrow_paths64(source);
    borrowed.delta = 7.0;
    borrowed_group.join_type = next::JoinType::Round;
    borrowed.arc_tolerance = 0.25;

    auto owning = next::offset_request64{};
    owning.paths = next::Paths64{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };
    owning.delta = borrowed.delta;
    owning.join_type = borrowed_group.join_type;
    owning.arc_tolerance = borrowed.arc_tolerance;

    const auto borrowed_result = next::offset_stage_checked(borrowed);
    const auto owning_result = next::offset_checked(owning);

    ASSERT_TRUE(borrowed_result.has_value());
    ASSERT_TRUE(owning_result.has_value());
    EXPECT_EQ(materialize(borrowed_result->paths), owning_result->closed);
}

TEST(Clipper2NextBorrowedOffsetApiTests, InputOutputAndWorkspaceLimitsFailClosed) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
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

TEST(Clipper2NextBorrowedOffsetApiTests, ZeroDeltaStillUsesTheBorrowedSingleCopyContract) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->paths.size(), 1U);
    EXPECT_EQ(result->stats.input_collection_point_writes, 0U);
    EXPECT_EQ(result->stats.engine_input_point_writes, 4U);
    EXPECT_EQ(result->stats.output_point_count, 4U);
}

TEST(Clipper2NextBorrowedOffsetApiTests, ZeroDeltaRetainsOpenPathSemanticsInTheSharedDescriptor) {
    const auto source = foreign_paths{{{0, 0}, {100, 0}}};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request_group.end_type = next::EndType::Butt;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->paths.size(), 1U);
    EXPECT_EQ(result->paths[0].closure, geotypes::PathClosure::Open);
}

TEST(Clipper2NextBorrowedOffsetApiTests, CoordinatesOutsideTheOffsetKernelRangeFailExplicitly) {
    const auto source = foreign_paths{{
        {(std::numeric_limits<std::int64_t>::max)(), 0},
        {0, 0},
        {0, 1},
    }};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 1.0;

    const auto result = next::offset_stage_checked(request);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     OffsetExecutionIsIndependentOfAndRestoresTheCallingRoundingMode) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 2.5;
    request_group.join_type = next::JoinType::Miter;
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

TEST(Clipper2NextBorrowedOffsetApiTests, ExplicitAwayFromZeroRoundingPreservesHalfUnitOffsets) {
    const auto source = foreign_paths{rectangle(0, 0, 4, 4)};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 0.5;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;
    request.coordinate_rounding = geotypes::CoordinateRounding::NearestAwayFromZero;

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

TEST(Clipper2NextBorrowedOffsetApiTests, ExplicitQuadrantSegmentsOwnRoundJoinResolution) {
    const auto source = foreign_paths{rectangle(0, 0, 100, 100)};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 20.0;
    request_group.join_type = next::JoinType::Round;
    request_group.end_type = next::EndType::Polygon;
    request.arc_segments_per_quadrant = 1U;

    const auto coarse = next::offset_stage_checked(request);
    ASSERT_TRUE(coarse.has_value());

    request.arc_segments_per_quadrant = 8U;
    const auto fine = next::offset_stage_checked(request);
    ASSERT_TRUE(fine.has_value());

    EXPECT_GT(fine->paths.point_count(), coarse->paths.point_count());
}

TEST(Clipper2NextBorrowedOffsetApiTests, ExplicitExecutorPreservesSerialResultAndInputOrder) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial.has_value());
    auto executor_state = recording_executor{};
    const auto executor =
        next::sync_bulk_executor_ref{&executor_state, 16U, &execute_recorded_chunks};

    const auto concurrent = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(concurrent.has_value());
    EXPECT_GT(executor_state.invocation_count, 0U);
    EXPECT_EQ(materialize(concurrent->paths), materialize(serial->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests,
     ExecutorCapabilityAboveKernelMaximumUsesOneEffectiveConcurrency) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;
    auto reference_state = recording_executor{};
    const auto reference_executor =
        next::sync_bulk_executor_ref{&reference_state, 16U, &execute_recorded_chunks};
    const auto reference = next::offset_stage_checked(request, reference_executor);
    ASSERT_TRUE(reference.has_value());

    auto wide_state = recording_executor{};
    const auto wide_executor =
        next::sync_bulk_executor_ref{&wide_state, 1'000U, &execute_recorded_chunks};
    const auto result = next::offset_stage_checked(request, wide_executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(wide_state.requested_concurrency, 16U);
    EXPECT_EQ(wide_state.item_count, reference_state.item_count);
    EXPECT_EQ(wide_state.item_count, 64U);
    EXPECT_EQ(result->stats.planned_engine_workspace_bytes,
              reference->stats.planned_engine_workspace_bytes);
    EXPECT_EQ(materialize(result->paths), materialize(reference->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests, ExecutorFailureIsNotReportedAsGeometryFailure) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    auto executor_state =
        recording_executor{.result = next::bulk_execution_error::scheduler_failure};
    const auto executor =
        next::sync_bulk_executor_ref{&executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::executor_failure);
}

TEST(Clipper2NextBorrowedOffsetApiTests, ParallelWorkspaceSharesTheDirectPreparationPeak) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial.has_value());
    request.limits.maximum_engine_workspace_bytes = serial->stats.planned_engine_workspace_bytes;
    auto executor_state = recording_executor{};
    const auto executor =
        next::sync_bulk_executor_ref{&executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_GT(executor_state.invocation_count, 0U);
    EXPECT_EQ(result->stats.planned_engine_workspace_bytes,
              serial->stats.planned_engine_workspace_bytes);
    EXPECT_EQ(materialize(result->paths), materialize(serial->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests, ActualCleanupSizeAdmitsDenseDisjointParallelOffset) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;
    const auto reference = next::offset_stage_checked(request);
    ASSERT_TRUE(reference.has_value());
    request.limits.maximum_engine_work = 2'147'483'648ULL;
    request.limits.maximum_engine_workspace_bytes = 128ULL * 1024ULL * 1024ULL * 1024ULL;
    auto executor_state = recording_executor{};
    const auto executor =
        next::sync_bulk_executor_ref{&executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value()) << "error=" << static_cast<int>(result.error())
                                    << " reference_points=" << reference->stats.output_point_count;
    EXPECT_TRUE(result->stats.output_is_disjoint_simple_shells);
    EXPECT_GT(executor_state.invocation_count, 0U);
    EXPECT_LE(result->stats.planned_engine_work, request.limits.maximum_engine_work);
    EXPECT_LE(result->stats.planned_engine_workspace_bytes,
              request.limits.maximum_engine_workspace_bytes);
    EXPECT_EQ(materialize(result->paths), materialize(reference->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests, NegativeOffsetDoesNotClaimDisjointShellTopology) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = -5.0;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->stats.output_is_disjoint_simple_shells);
}

TEST(Clipper2NextBorrowedOffsetApiTests, DirectTopologyCertificateValidatesTheActualRoundedOutput) {
    constexpr auto point_count = std::size_t{1'140U};
    constexpr auto radius = 1'000'000.0;
    auto path = next::Path64{};
    path.reserve(point_count);
    for (auto index = std::size_t{}; index < point_count; ++index) {
        const auto angle =
            2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(point_count);
        path.push_back(next::Point64{
            static_cast<std::int64_t>(std::llround(radius * std::cos(angle))),
            static_cast<std::int64_t>(std::llround(radius * std::sin(angle))),
        });
    }
    const auto source = next::Paths64{std::move(path)};
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 64.0;
    request_group.join_type = next::JoinType::Round;
    request_group.end_type = next::EndType::Polygon;
    request.arc_segments_per_quadrant = 8U;

    const auto result = next::offset_stage_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->paths.size(), 1U);
    EXPECT_FALSE(result->stats.output_is_disjoint_simple_shells);
    EXPECT_TRUE(next::internal::path_simplicity::path_is_provably_simple(
        result->paths.front(), result->paths.front().size()));
}

TEST(Clipper2NextBorrowedOffsetApiTests, ConcurrentExecutorIsByteExactWithSerialOffset) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial.has_value());
    auto executor_state = threaded_executor{.worker_count = 16U};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, executor_state.worker_count, &execute_threaded_chunks};

    const auto concurrent = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(concurrent.has_value());
    EXPECT_GT(executor_state.invocation_count.load(std::memory_order_relaxed), 1U);
    EXPECT_EQ(materialize(concurrent->paths), materialize(serial->paths));
}

TEST(Clipper2NextBorrowedOffsetApiTests, SubthresholdOffsetRemainsSerialWithAnExecutor) {
    const auto source = subthreshold_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    auto executor_state = recording_executor{};
    const auto executor =
        next::sync_bulk_executor_ref{&executor_state, 16U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(executor_state.invocation_count, 0U);
}

TEST(Clipper2NextBorrowedOffsetApiTests, InsufficientConcurrencyRemainsSerial) {
    const auto source = parallel_eligible_source();
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = 5.0;
    auto executor_state = recording_executor{};
    const auto executor =
        next::sync_bulk_executor_ref{&executor_state, 8U, &execute_recorded_chunks};

    const auto result = next::offset_stage_checked(request, executor);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(executor_state.invocation_count, 0U);
}

namespace {
struct reentering_executor {
    bool nested{};
    std::size_t calls{};
};
auto execute_reentering(void* opaque, std::size_t count, std::size_t,
                       std::size_t, next::bulk_task_ref task) noexcept
    -> next::bulk_execution_error {
    auto& executor = *static_cast<reentering_executor*>(opaque);
    ++executor.calls;
    try {
        if (executor.nested) {
            auto request = next::offset_request64{};
            request.paths = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
            request.delta = 50;
            if (!next::offset_checked(request)) { return next::bulk_execution_error::scheduler_failure; }
        } else {
            next::release_thread_caches();
            next::release_thread_caches();
        }
        for (std::size_t index = 0; index < count; ++index) { task(index, index + 1); }
        return next::bulk_execution_error::none;
    } catch (...) { return next::bulk_execution_error::scheduler_failure; }
}
}

TEST(Clipper2NextBorrowedOffsetApiTests, SynchronousExecutorReentryPreservesFollowingGroup) {
    const auto dense = parallel_eligible_source();
    const auto square = foreign_paths{rectangle(2'000'000, 0, 2'000'100, 100)};
    const next::borrowed_offset_group64 groups[]{
        {next::borrow_paths64(dense), next::JoinType::Round, next::EndType::Polygon},
        {next::borrow_paths64(square), next::JoinType::Round, next::EndType::Polygon}};
    const auto request = next::borrowed_offset_request64{.groups = groups, .delta = 5};
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial);
    for (const bool nested : {false, true}) {
        auto executor = reentering_executor{nested};
        const auto actual = next::offset_stage_checked(request, {&executor, 16, &execute_reentering});
        ASSERT_TRUE(actual);
        EXPECT_GT(executor.calls, 0U);
        EXPECT_EQ(materialize(actual->paths), materialize(serial->paths));
        const auto bounds = next::bounds(materialize(actual->paths).back());
        EXPECT_EQ(bounds.left, 1'999'995);
        EXPECT_EQ(bounds.right, 2'000'105);
    }
}

TEST(Clipper2NextBorrowedOffsetApiTests, MixedJoinedPathsExecuteParallelAndSurviveReentry) {
    auto source = parallel_eligible_source();
    for (std::size_t i = 0; i < source.size(); ++i) {
        source[i] = dense_rectangle(static_cast<std::int64_t>(i) * 2'000, 0, 300);
    }
    for (std::size_t i = 0; i < source.size(); i += 8) {
        const auto x = static_cast<std::int64_t>(i) * 2'000;
        source[i] = {{x, 0}, {x + 100, 0}};
    }
    const auto group = next::borrowed_offset_group64{
        next::borrow_paths64(source), next::JoinType::Round, next::EndType::Joined};
    const auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5};
    const auto serial = next::offset_stage_checked(request);
    ASSERT_TRUE(serial);
    auto executor = reentering_executor{true};
    const auto actual = next::offset_stage_checked(request, {&executor, 16, &execute_reentering});
    ASSERT_TRUE(actual);
    EXPECT_GT(executor.calls, 0U);
    EXPECT_EQ(materialize(actual->paths), materialize(serial->paths));
}

namespace {
struct callback_offset_source {
    next::Paths64 paths;
    std::function<void()> on_size;
    auto begin() const { return paths.begin(); }
    auto end() const { return paths.end(); }
    auto size() const { on_size(); return paths.size(); }
};
}
TEST(Clipper2NextBorrowedOffsetApiTests, SourceCardinalityLeaseReentryFailureAndRetry) {
    next::internal::offset_state* cached;
    { auto lease = next::internal::offset_state_lease{}; cached = &lease.state(); }
    bool callback_seen = false;
    bool reject = false;
    callback_offset_source source{{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}}, [&] {
        callback_seen = true;
        auto nested = next::internal::offset_state_lease{};
        EXPECT_NE(&nested.state(), cached);
        next::release_thread_caches();
        if (reject) { throw std::runtime_error("source failed"); }
    }};
    const auto group = next::borrowed_offset_group64{next::borrow_paths64(source), next::JoinType::Miter, next::EndType::Polygon};
    const auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5};
    ASSERT_TRUE(next::offset_stage_checked(request));
    EXPECT_TRUE(callback_seen);
    reject = true;
    EXPECT_FALSE(next::offset_stage_checked(request));
    reject = false;
    const auto retried = next::offset_stage_checked(request);
    ASSERT_TRUE(retried);
    ASSERT_EQ(retried->paths.size(), 1U);
    EXPECT_EQ(next::bounds(materialize(retried->paths)).left, -5);
    EXPECT_EQ(next::bounds(materialize(retried->paths)).right, 105);
}

#include "api/private/borrowed_offset_execution.h"
#include "api/private/checked_execution.h"
#include "offset/private/offset_group.h"
#include <array>

namespace {
auto observed_offset(const next::borrowed_offset_request64& request,
                     next::internal::offset_engine_resource_context& resources,
                     next::sync_bulk_executor_ref executor = {})
    -> next::expected_borrowed_offset_stage_result64 {
    return next::internal::checked_call<next::borrowed_offset_stage_result64>([&] {
        resources.reset_usage();
        resources.generation = {};
        resources.selected = {};
        return next::internal::execute_borrowed_offset_stage(request, executor, resources);
    });
}
}

TEST(Clipper2NextBorrowedOffsetApiTests, CompleteLedgerExactStageLimitsAndCleanupFailureRetry) {
    const next::Paths64 paths{{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
                             {{50, 0}, {150, 0}, {150, 100}, {50, 100}}};
    const auto group = next::borrowed_offset_group64{next::borrow_paths64(paths), next::JoinType::Round, next::EndType::Polygon};
    auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 10, .arc_segments_per_quadrant = 8};
    auto reference_context = next::internal::offset_engine_resource_context{};
    const auto reference = observed_offset(request, reference_context);
    ASSERT_TRUE(reference);
    ASSERT_GT(reference_context.work, reference_context.generation.work);
    std::printf("T09 cleanup work=%zu generation=%zu engine_peak=%zu stage_peak=%zu plan=%zu live=%zu/%zu\n",
        reference_context.work, reference_context.generation.work, reference_context.engine_high_water,
        reference_context.staging_high_water, reference->stats.planned_engine_workspace_bytes,
        reference_context.engine_bytes, reference_context.staging_bytes);
    EXPECT_EQ(reference_context.work, reference->stats.planned_engine_work);
    EXPECT_EQ(reference_context.staging_high_water, reference->stats.peak_workspace_bytes);
    EXPECT_LE(reference_context.engine_high_water, reference->stats.planned_engine_workspace_bytes);
    EXPECT_EQ(reference_context.engine_bytes, 0U);
    EXPECT_EQ(reference_context.staging_bytes, 0U);
    for (bool cold : {true, false}) {
        if (cold) { next::release_thread_caches(); }
        request.limits.maximum_staging_workspace_bytes = reference_context.staging_high_water;
        request.limits.maximum_engine_work = reference->stats.planned_engine_work;
        request.limits.maximum_engine_workspace_bytes = reference->stats.planned_engine_workspace_bytes;
        auto exact_context = next::internal::offset_engine_resource_context{};
        auto exact = observed_offset(request, exact_context);
        ASSERT_TRUE(exact);
        EXPECT_EQ(materialize(exact->paths), materialize(reference->paths));
        EXPECT_EQ(exact_context.staging_high_water, reference_context.staging_high_water);
        --request.limits.maximum_staging_workspace_bytes;
        auto failed_context = next::internal::offset_engine_resource_context{};
        const auto failed = observed_offset(request, failed_context);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error(), next::clipper_error_code::resource_limit);
        EXPECT_GT(failed_context.work, failed_context.generation.work);
        EXPECT_GT(failed_context.engine_high_water, 0U);
        EXPECT_EQ(failed_context.staging_bytes, 0U);
        EXPECT_EQ(failed_context.engine_bytes, 0U);
    }
    request.limits = {};
    ASSERT_TRUE(next::offset_stage_checked(request));
    next::release_thread_caches();

    // Point construction is committed before source copying, and descriptor
    // admission happens afterward without retaining any consumed headroom.
    struct observed_paths_range {
        next::Paths64 paths;
        std::function<void()> inspect;
        auto begin() const { inspect(); return paths.begin(); }
        auto end() const { return paths.end(); }
        auto size() const { return paths.size(); }
    };
    const auto measurement_bytes = sizeof(next::path_source_contract::borrowed_path_measurement64);
    const auto point_prefix = measurement_bytes + 4U * sizeof(next::Point64);
    for (const auto limit : std::array<std::size_t, 3>{point_prefix - 1U, point_prefix,
             point_prefix + sizeof(next::internal::offset_path_record) - 1U}) {
        auto context = next::internal::offset_engine_resource_context{};
        bool copied = false;
        observed_paths_range source{{{{0,0},{100,0},{100,100},{0,100}}}, [&] {
            if (context.staging_bytes >= point_prefix) {
                copied = true;
                EXPECT_EQ(context.staging_bytes, point_prefix);
                EXPECT_EQ(context.reserved_staging_bytes, 0U);
            }
        }};
        const auto traced_group = next::borrowed_offset_group64{
            next::borrow_paths64(source), next::JoinType::Miter, next::EndType::Polygon};
        auto traced_request = next::borrowed_offset_request64{
            .groups = std::span{&traced_group, 1U}, .delta = 0};
        traced_request.limits.maximum_staging_workspace_bytes = limit;
        const auto failed = observed_offset(traced_request, context);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error(), next::clipper_error_code::resource_limit);
        EXPECT_EQ(copied, limit >= point_prefix);
        EXPECT_EQ(context.staging_high_water,
                  limit >= point_prefix ? point_prefix : measurement_bytes);
        EXPECT_EQ(context.work, 0U);
        EXPECT_EQ(context.staging_bytes, 0U);
        EXPECT_EQ(context.reserved_staging_bytes, 0U);
    }
}

TEST(Clipper2NextBorrowedOffsetApiTests, FinalOutputLimitDoesNotCapRawGeneration) {
    const next::Paths64 paths{{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
                             {{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    const auto group = next::borrowed_offset_group64{next::borrow_paths64(paths), next::JoinType::Miter, next::EndType::Polygon};
    auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5};
    request.limits.maximum_output_path_count = 1;
    request.limits.maximum_output_point_count = 4;
    auto context = next::internal::offset_engine_resource_context{};
    const auto result = observed_offset(request, context);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->paths.size(), 1U);
    EXPECT_EQ(result->paths.point_count(), 4U);
    EXPECT_GT(context.work, context.generation.work);
    EXPECT_EQ(context.engine_bytes, 0U);
    EXPECT_EQ(context.staging_bytes, 0U);
}

TEST(Clipper2NextBorrowedOffsetApiTests, CleanupPlanWorkAndBytesRejectOneUnderAfterRealGeneration) {
    const next::Paths64 paths{{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
                             {{50, 0}, {150, 0}, {150, 100}, {50, 100}}};
    const auto group = next::borrowed_offset_group64{next::borrow_paths64(paths), next::JoinType::Miter, next::EndType::Polygon};
    auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 10};
    auto context = next::internal::offset_engine_resource_context{};
    const auto baseline = observed_offset(request, context);
    ASSERT_TRUE(baseline);
    ASSERT_GT(context.work, context.generation.work);
    const auto work = baseline->stats.planned_engine_work;
    const auto bytes = baseline->stats.planned_engine_workspace_bytes;
    for (bool limit_work : {true, false}) {
        request.limits.maximum_engine_work = work - static_cast<std::size_t>(limit_work);
        request.limits.maximum_engine_workspace_bytes = bytes - static_cast<std::size_t>(!limit_work);
        const auto limited = observed_offset(request, context);
        ASSERT_FALSE(limited);
        EXPECT_EQ(limited.error(), next::clipper_error_code::resource_limit);
        EXPECT_GT(context.engine_high_water, 0U);
        EXPECT_EQ(context.work, context.generation.work);
        EXPECT_EQ(context.engine_bytes, 0U);
        EXPECT_EQ(context.staging_bytes, 0U);
        request.limits.maximum_engine_work = work;
        request.limits.maximum_engine_workspace_bytes = bytes;
        const auto retry = observed_offset(request, context);
        ASSERT_TRUE(retry);
        EXPECT_EQ(materialize(retry->paths), materialize(baseline->paths));
    }
}

#include "offset/private/offset_group_processor.h"
TEST(Clipper2NextBorrowedOffsetApiTests, RealParallelMergeChargesDestinationWhileChunksRemainLive) {
    const auto source = parallel_eligible_source();
    auto prototype = next::Path64{};
    for (const auto point : source.front()) { prototype.emplace_back(point.x, point.y); }
    const auto native = next::internal::offset_group{next::Paths64{prototype}, next::JoinType::Miter, next::EndType::Polygon};
    auto state = next::internal::offset_state{};
    auto raw = next::path_set64{};
    next::internal::build_offset_group_paths(state, native, {}, 5, nullptr, raw);
    ASSERT_EQ(raw.size(), 1U);
    const auto one_raw = raw.point_count() * sizeof(next::Point64) + sizeof(geotypes::PathDescriptor);
    const auto group = next::borrowed_offset_group64{next::borrow_paths64(source), next::JoinType::Miter, next::EndType::Polygon};
    const auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5};
    auto root = next::internal::offset_engine_resource_context{};
    auto executor = recording_executor{};
    const auto result = observed_offset(request, root, {&executor, 16, &execute_recorded_chunks});
    ASSERT_TRUE(result);
    ASSERT_EQ(executor.item_count, 64U);
    // 512 equal-size real paths form 64 chunks of 8. Sequential worker
    // scheduling has a smaller scratch peak; merge grows all 8 destination
    // paths before releasing that consumed chunk.
    EXPECT_EQ(root.engine_high_water, (512U + 8U) * one_raw);
    std::printf("T09 parallel merge raw_per_path=%zu actual_peak=%zu live=%zu/%zu reserved=%zu\n",
        one_raw, root.engine_high_water, root.engine_bytes, root.staging_bytes, root.reserved_engine_bytes);
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
    EXPECT_LE(root.engine_high_water, result->stats.planned_engine_workspace_bytes);
}

TEST(Clipper2NextBorrowedOffsetApiTests, ThreadedWorkerFailureQuiescesAndReleasesReservationBeforeRetry) {
    auto source = parallel_eligible_source();
    source.back() = dense_rectangle(next::MAX_COORD - 256, 0, 256);
    const auto group = next::borrowed_offset_group64{next::borrow_paths64(source), next::JoinType::Miter, next::EndType::Polygon};
    const auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5};
    auto root = next::internal::offset_engine_resource_context{};
    auto executor = threaded_executor{.worker_count = 16};
    const auto ref = next::sync_bulk_executor_ref{&executor, 16, &execute_threaded_chunks};
    const auto failed = observed_offset(request, root, ref);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error(), next::clipper_error_code::coordinate_range);
    EXPECT_GT(executor.invocation_count.load(), 0U);
    EXPECT_GT(root.engine_high_water, 0U);
    std::printf("T09 worker failure engine_peak=%zu stage_peak=%zu live=%zu/%zu reserved=%zu\n",
        root.engine_high_water, root.staging_high_water, root.engine_bytes, root.staging_bytes, root.reserved_engine_bytes);
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
    source.back() = dense_rectangle(511 * 2000, 0, 256);
    const auto retry = observed_offset(request, root, ref);
    ASSERT_TRUE(retry);
    EXPECT_EQ(retry->paths.size(), 512U);
    EXPECT_EQ(retry->paths.point_count(), 2048U);
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
}

TEST(Clipper2NextBorrowedOffsetApiTests, SkewedParallelPathsReuseReportedExactBudget) {
    auto source = foreign_paths{};
    for (std::int64_t i = 0; i < 512; ++i) {
        source.push_back(i < 64 ? dense_rectangle(i * 10000, 0, 2048)
                                : rectangle(i * 10000, 0, i * 10000 + 100, 100));
    }
    const auto group = next::borrowed_offset_group64{next::borrow_paths64(source), next::JoinType::Miter, next::EndType::Polygon};
    auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5};
    auto root = next::internal::offset_engine_resource_context{};
    auto executor = recording_executor{};
    const auto ref = next::sync_bulk_executor_ref{&executor, 16, &execute_recorded_chunks};
    const auto baseline = observed_offset(request, root, ref);
    ASSERT_TRUE(baseline);
    request.limits.maximum_engine_workspace_bytes = baseline->stats.planned_engine_workspace_bytes;
    const auto exact = observed_offset(request, root, ref);
    ASSERT_TRUE(exact);
    EXPECT_EQ(materialize(exact->paths), materialize(baseline->paths));
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
}

TEST(Clipper2NextBorrowedOffsetApiTests, SerialGroupScratchEndsBeforeTheParallelPhase) {
    using namespace next::internal;
    const auto foreign = parallel_eligible_source();
    auto paths = next::Paths64{};
    for (const auto& source : foreign) {
        auto& path = paths.emplace_back();
        for (auto point : source) { path.emplace_back(point.x, point.y); }
    }
    const auto large = offset_group{std::move(paths), next::JoinType::Miter, next::EndType::Polygon};
    auto small_path = next::Path64{};
    for (auto point : dense_rectangle(-10000, 0, 1024)) { small_path.emplace_back(point.x, point.y); }
    const auto small = offset_group{next::Paths64{std::move(small_path)}, next::JoinType::Miter, next::EndType::Polygon};
    const auto prototype = offset_group{next::Paths64{next::Path64(large.path(0).begin(), large.path(0).end())},
        next::JoinType::Miter, next::EndType::Polygon};
    auto raw_one = next::path_set64{};
    auto prototype_state = offset_state{};
    build_offset_group_paths(prototype_state, prototype, {}, 5, nullptr, raw_one);
    const auto one_raw = raw_one.point_count() * sizeof(next::Point64) + sizeof(geotypes::PathDescriptor);
    auto root = execution_resource_ledger{};
    {
        auto output = next::path_set64{};
        auto owner = scoped_workspace_charge{&root};
        auto state = offset_state{};
        auto scope = offset_resource_scope{state, &root, &owner};
        build_offset_group_paths(state, small, {}, 5, nullptr, output);
        const auto retained_small = owner.bytes();
        ASSERT_FALSE(state.normals.empty());
        ASSERT_FALSE(state.path_out.empty());
        const auto plan = plan_offset_generation_resources(512, 512 * 1024, 1024, 5,
            next::JoinType::Miter, next::EndType::Polygon, 0, 0, 16);
        root.maximum_workspace_bytes = retained_small + plan.retained_output_bytes + 16U * plan.scratch_bytes;
        auto executor = recording_executor{};
        ASSERT_NO_THROW(build_offset_group_paths_parallel(state, large, {}, 5,
            {&executor, 16, &execute_recorded_chunks}, output));
        EXPECT_EQ(executor.item_count, 64U);
        EXPECT_TRUE(state.normals.empty());
        EXPECT_TRUE(state.path_out.empty());
        EXPECT_EQ(root.engine_bytes, owner.bytes());
        EXPECT_EQ(root.engine_high_water, retained_small + (512U + 8U) * one_raw);
        EXPECT_EQ(root.reserved_engine_bytes, 0U);
        EXPECT_EQ(root.reserved_staging_bytes, 0U);
    }
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
}

TEST(Clipper2NextBorrowedOffsetApiTests, EmptyGroupsNeedNoGenerationBudget) {
    for (const auto& paths : {next::Paths64{}, next::Paths64{next::Path64{}}}) {
        const auto group = next::borrowed_offset_group64{next::borrow_paths64(paths), next::JoinType::Round, next::EndType::Round};
        auto request = next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5};
        request.limits.maximum_engine_workspace_bytes = 0;
        request.limits.maximum_engine_work = 0;
        if (paths.empty()) { request.limits.maximum_staging_workspace_bytes = 0; }
        auto context = next::internal::offset_engine_resource_context{};
        const auto result = observed_offset(request, context);
        ASSERT_TRUE(result);
        EXPECT_TRUE(result->paths.empty());
        EXPECT_EQ(result->stats.planned_engine_work, 0U);
        EXPECT_EQ(result->stats.planned_engine_workspace_bytes, 0U);
        EXPECT_EQ(context.engine_high_water, 0U);
        EXPECT_EQ(context.engine_bytes, 0U);
        EXPECT_EQ(context.staging_bytes, 0U);
    }
}
