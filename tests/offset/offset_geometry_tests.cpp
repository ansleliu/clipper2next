#include "clip/engine/private/engine_buffer.h"
#include "geometry/private/trim_collinear.h"
#include "clipper2next/geotypes/checked_coordinate_cast.hpp"
#include "offset/private/offset_parallel_results.h"
#include <array>
#include <atomic>
#include <barrier>
#include <cfenv>
#include <cerrno>
#include <chrono>
#include <thread>
#include <gtest/gtest.h>

#include "offset/private/offset_geometry.h"
#include "geometry/private/integer_area_sign.h"
#include "clipper2next/api/error.h"
#include "support/test_paths.h"

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetGeometryTests, ExactAreaSignHandlesWideCancellationAndZero) {
    const auto m = next::MAX_COORD;
    for (const std::int64_t translation : {0LL, 10000000000LL}) {
        next::Path64 thin{{0, translation}, {1000003, translation + 1}, {1000002, translation + 1}};
        EXPECT_EQ(next::internal::signed_area_sign(thin), 1);
        std::ranges::reverse(thin);
        EXPECT_EQ(next::internal::signed_area_sign(thin), -1);
    }
    next::Path64 wide{{-m, -m}, {m, -m}, {m, m}, {-m, m}};
    EXPECT_EQ(next::internal::signed_area_sign(wide), 1);
    std::ranges::reverse(wide);
    EXPECT_EQ(next::internal::signed_area_sign(wide), -1);
    // Opposite loops cancel exactly after accumulating values beyond 128 bits.
    next::Path64 cancel;
    for (int repeat = 0; repeat < 64; ++repeat) { cancel.insert(cancel.end(), wide.begin(), wide.end()); }
    EXPECT_EQ(next::internal::signed_area_sign(cancel), -1);
    std::ranges::reverse(wide);
    for (int repeat = 0; repeat < 64; ++repeat) { cancel.insert(cancel.end(), wide.begin(), wide.end()); }
    EXPECT_EQ(next::internal::signed_area_sign(cancel), 0);
}

TEST(Clipper2NextOffsetGeometryTests, ExactAreaSignIsDefinedBeforeCoordinateAdmission) {
    const auto minimum = (std::numeric_limits<std::int64_t>::min)();
    next::Path64 triangle{{minimum, minimum}, {minimum, minimum + 1}, {0, minimum}};
    // Base 2^63, height 1: the clockwise doubled area is exactly -2^63.
    EXPECT_EQ(next::internal::signed_area_sign(triangle), -1);
    std::ranges::reverse(triangle);
    EXPECT_EQ(next::internal::signed_area_sign(triangle), 1);
}

TEST(Clipper2NextOffsetGeometryTests, UnitNormalMatchesLegacyOrientation) {
    const auto normal = next::internal::unit_normal({0, 0}, {10, 0});

    EXPECT_DOUBLE_EQ(normal.x, 0.0);
    EXPECT_DOUBLE_EQ(normal.y, -1.0);
}

TEST(Clipper2NextOffsetGeometryTests, BuildNormalsClosesThePath) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10, 0, 10});

    const auto normals = next::internal::build_normals(path);

    ASSERT_EQ(normals.size(), path.size());
    EXPECT_DOUBLE_EQ(normals.front().x, 0.0);
    EXPECT_DOUBLE_EQ(normals.front().y, -1.0);
    EXPECT_DOUBLE_EQ(normals.back().x, -1.0);
    EXPECT_DOUBLE_EQ(normals.back().y, -0.0);
}

TEST(Clipper2NextOffsetGeometryTests, ArcParametersUseLegacyDefaultToleranceWhenUnset) {
    const auto parameters = next::internal::make_arc_parameters(100.0, 0.0);

    EXPECT_GT(parameters.steps_per_rad, 0.0);
    EXPECT_GT(parameters.step_sin, 0.0);
    EXPECT_LT(parameters.step_cos, 1.0);
}

TEST(Clipper2NextOffsetGeometryTests, GeneratedNonFiniteCoordinatesCannotBecomeIntegerZero) {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(next::internal::perpendicular_point({0, 0}, {nan, 0}, 1.0)),
                 next::clipper_error);
    EXPECT_THROW(static_cast<void>(next::internal::perpendicular_point(
                     {0, 0}, {1, 0}, std::numeric_limits<double>::infinity())),
                 next::clipper_error);
}
#include "support/private/engine_resource_plan.h"
#include "offset/private/offset_group_processor.h"

TEST(Clipper2NextOffsetGeometryTests, NonRoundResourcePlanPreservesAmbientFloatingPointState) {
    auto environment = std::fenv_t{};
    const auto saved_errno = errno;
    ASSERT_EQ(std::fegetenv(&environment), 0);
    struct restore_environment {
        std::fenv_t* value;
        int error;
        ~restore_environment() { std::fesetenv(value); errno = error; }
    } restore{&environment, saved_errno};
    for (const auto join : {next::JoinType::Miter, next::JoinType::Square, next::JoinType::Bevel}) {
        for (const auto end : {next::EndType::Polygon, next::EndType::Joined,
                              next::EndType::Butt, next::EndType::Square}) {
            for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
                SCOPED_TRACE(mode);
                ASSERT_EQ(std::fesetround(mode), 0);
                for (const auto seed_exception : {false, true}) {
                    std::feclearexcept(FE_ALL_EXCEPT);
                    if (seed_exception) { std::feraiseexcept(FE_DIVBYZERO); }
                    const auto expected_flags = std::fetestexcept(FE_ALL_EXCEPT);
                    errno = EDOM;
                    const auto plan = next::internal::plan_offset_generation_resources(
                        1, 6, 6, 18.0, join, end, 0.0, 0U, 1U);
                    const auto actual_errno = errno;
                    const auto actual_flags = std::fetestexcept(FE_ALL_EXCEPT);
                    const auto actual_round = std::fegetround();
                    EXPECT_EQ(actual_errno, EDOM);
                    EXPECT_EQ(actual_flags, expected_flags);
                    EXPECT_EQ(actual_round, mode);
                    EXPECT_GT(plan.work, 0U);
                }
            }
        }
    }
}

TEST(Clipper2NextOffsetGeometryTests, FullCirclePlanCoversSimultaneousGenerationOwners) {
    const auto group = next::internal::offset_group{next::Paths64{{{0, 0}}}, next::JoinType::Round, next::EndType::Round};
    auto state = next::internal::offset_state{};
    auto raw = next::path_set64{};
    const auto options = next::internal::offset_group_execution_options{.arc_segments_per_quadrant = 8};
    next::internal::build_offset_group_paths(state, group, options, 100.0, nullptr, raw);
    ASSERT_EQ(state.path_out.size(), 32U);
    ASSERT_EQ(raw.point_count(), 32U);
    const auto actual = (state.path_out.size() + raw.point_count()) * sizeof(next::Point64)
        + raw.size() * sizeof(geotypes::PathDescriptor);
    const auto plan = next::internal::plan_offset_generation_resources(1, 1, 1, 100.0,
        next::JoinType::Round, next::EndType::Round, 0.0, 8, 1);
    EXPECT_GE(plan.workspace_bytes, actual);
}


TEST(Clipper2NextOffsetGeometryTests, CircleOwnerAdmissionIsExactColdAndWarm) {
    const auto group = next::internal::offset_group{next::Paths64{{{0, 0}}}, next::JoinType::Round, next::EndType::Round};
    auto state = next::internal::offset_state{};
    const auto options = next::internal::offset_group_execution_options{.arc_segments_per_quadrant = 8};
    constexpr auto exact = 64U * sizeof(next::Point64) + sizeof(geotypes::PathDescriptor);
    for (bool warm : {false, true}) {
        if (warm) { state.path_out.reserve(4096); }
        for (auto limit : {std::size_t{0}, exact - 1, exact}) {
            auto resources = next::internal::offset_engine_resource_context{};
            resources.maximum_workspace_bytes = limit;
            bool succeeded = false;
            try {
                auto raw = next::path_set64{};
                auto output = next::internal::scoped_workspace_charge{&resources};
                auto scope = next::internal::offset_resource_scope{state, &resources, &output};
                next::internal::build_offset_group_paths(state, group, options, 100.0, nullptr, raw);
                ASSERT_EQ(raw.point_count(), 32U);
                ASSERT_EQ(state.path_out.size(), 32U);
                EXPECT_EQ(resources.engine_bytes, exact);
                succeeded = true;
            } catch (const next::clipper_error& error) {
                EXPECT_EQ(error.code(), next::clipper_error_code::resource_limit);
            }
            EXPECT_EQ(succeeded, limit == exact);
            EXPECT_EQ(resources.engine_bytes, 0U);
            EXPECT_EQ(resources.staging_bytes, 0U);
            EXPECT_EQ(state.path_out_charge.context(), nullptr);
        }
    }
}


TEST(Clipper2NextOffsetGeometryTests, ChargeTransferReclassifyOverflowAndRollbackConserveTotals) {
    using namespace next::internal;
    auto root = offset_engine_resource_context{};
    root.maximum_workspace_bytes = 64;
    root.maximum_staging_bytes = 32;
    {
        auto first = scoped_workspace_charge{&root};
        ASSERT_EQ(first.try_set_slots(4, 16), next::clipper_error_code::ok);
        auto moved = std::move(first);
        EXPECT_EQ(first.bytes(), 0U);
        EXPECT_EQ(first.context(), nullptr);
        EXPECT_EQ(root.engine_bytes, 64U);
        EXPECT_EQ(moved.try_reclassify(workspace_category::staging), next::clipper_error_code::resource_limit);
        EXPECT_EQ(root.engine_bytes, 64U);
        EXPECT_EQ(root.staging_bytes, 0U);
        ASSERT_EQ(moved.try_set_slots(2, 16), next::clipper_error_code::ok);
        ASSERT_EQ(moved.try_reclassify(workspace_category::staging), next::clipper_error_code::ok);
        EXPECT_EQ(root.engine_bytes, 0U);
        EXPECT_EQ(root.staging_bytes, 32U);
        EXPECT_EQ(moved.try_set_slots((std::numeric_limits<std::size_t>::max)(), 16), next::clipper_error_code::resource_limit);
        EXPECT_EQ(root.staging_bytes, 32U);
    }
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
    EXPECT_EQ(root.engine_high_water, 64U);
    EXPECT_EQ(root.staging_high_water, 32U);
    for (const auto category : {workspace_category::engine, workspace_category::staging}) {
        auto transferred = scoped_workspace_charge{&root, category};
        std::vector<next::Point64> points;
        {
            auto source = scoped_workspace_charge{&root, category};
            auto growth = scoped_workspace_growth{source, 2U, sizeof(next::Point64)};
            points.resize(2U);
            growth.commit();
            transferred = std::move(source);
            EXPECT_EQ(source.context(), nullptr);
            EXPECT_EQ(source.bytes(), 0U);
        }
        EXPECT_EQ(transferred.bytes(), points.size() * sizeof(next::Point64));
        EXPECT_EQ(root.reserved_engine_bytes, 0U);
        EXPECT_EQ(root.reserved_staging_bytes, 0U);
        points.clear();
        transferred.release();
        EXPECT_EQ(root.engine_bytes, 0U);
        EXPECT_EQ(root.staging_bytes, 0U);
    }
}

TEST(Clipper2NextOffsetGeometryTests, ShapePlansCoverJoinedCapsAndSinglePointFallback) {
    const next::Paths64 inputs[] = {{{{0, 0}}}, {{{0, 0}, {100, 0}}},
        {{{0, 0}, {100, 0}, {30, 30}, {100, 100}, {0, 100}}}};
    for (const auto& input : inputs) {
        for (auto end : {next::EndType::Polygon, next::EndType::Joined, next::EndType::Round, next::EndType::Square, next::EndType::Butt}) {
            for (auto join : {next::JoinType::Round, next::JoinType::Miter, next::JoinType::Bevel}) {
                for (auto tolerance : {0.0, 100.0}) {
                    auto group = next::internal::offset_group{input, join, end};
                    auto root = next::internal::offset_engine_resource_context{};
                    auto output_charge = next::internal::scoped_workspace_charge{&root};
                    auto state = next::internal::offset_state{};
                    auto scope = next::internal::offset_resource_scope{state, &root, &output_charge};
                    auto raw = next::path_set64{};
                    const auto options = next::internal::offset_group_execution_options{.arc_tolerance = tolerance};
                    next::internal::build_offset_group_paths(state, group, options, 100.0, nullptr, raw);
                    const auto plan = next::internal::plan_offset_generation_resources(1, input[0].size(), input[0].size(),
                        100.0, join, end, tolerance, 0, 1);
                    EXPECT_GE(plan.workspace_bytes, root.engine_high_water);
                    if (end == next::EndType::Joined && input[0].size() > 2) { EXPECT_EQ(raw.size(), 2U); }
                }
            }
        }
    }
}

#include "offset/private/offset_algorithm.h"
#include "clipper2next/offset.h"
TEST(Clipper2NextOffsetGeometryTests, EffectiveCallbackDeltaAdmitsBeforeArcAndNestedOperationIsIndependent) {
    using namespace next::internal;
    const auto groups = std::vector<offset_group>{offset_group{next::Paths64{{{0, 0}}}, next::JoinType::Round, next::EndType::Round}};
    auto root = offset_engine_resource_context{};
    root.maximum_workspace_bytes = 512;
    auto callback_count = 0U;
    const next::DeltaCallback64 callback = [&](const next::Path64& path, const next::PathD&, std::size_t, std::size_t) {
        ++callback_count;
        EXPECT_EQ(path.size(), 1U);
        const auto live = root.engine_bytes;
        const next::Paths64 square{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
        const auto group = next::borrowed_offset_group64{next::borrow_paths64(square), next::JoinType::Miter, next::EndType::Polygon};
        const auto nested = next::offset_stage_checked(next::borrowed_offset_request64{.groups = std::span{&group, 1U}, .delta = 5});
        EXPECT_TRUE(nested);
        EXPECT_EQ(root.engine_bytes, live);
        return 1000.0;
    };
    auto state = offset_state{};
    auto output = next::path_set64{};
    bool limited = false;
    try {
        execute_offset_algorithm(state, groups, 1.0, output,
            offset_algorithm_options{.arc_tolerance = 1000.0}, next::delta_callback_ref{callback}, {}, &root);
    } catch (const next::clipper_error& error) { limited = error.code() == next::clipper_error_code::resource_limit; }
    EXPECT_TRUE(limited);
    EXPECT_EQ(callback_count, 1U);
    EXPECT_GT(root.work, 0U);
    EXPECT_EQ(root.engine_high_water, sizeof(next::Point64)); // callback copy only; full arc denied before generation
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
    EXPECT_TRUE(state.path_out.empty());
    EXPECT_EQ(state.path_out_charge.context(), nullptr);
}

#include "clip/engine/private/engine_buffer.h"
TEST(Clipper2NextOffsetGeometryTests, BufferMoveAndIntersectionScratchSwapKeepSameRootCharges) {
    using namespace next::internal;
    auto root = execution_resource_ledger{};
    root.maximum_workspace_bytes = 5U * sizeof(next::Point64);
    auto first = engine_buffer<next::Point64>{};
    auto second = engine_buffer<next::Point64>{};
    first.bind(&root);
    second.bind(&root);
    first.resize(2);
    second.resize(3);
    first[0] = {1, 2};
    second[0] = {3, 4};
    first.swap(second);
    EXPECT_EQ(first.size(), 3U);
    EXPECT_EQ(second.size(), 2U);
    EXPECT_EQ(first[0], next::Point64(3, 4));
    EXPECT_EQ(second[0], next::Point64(1, 2));
    EXPECT_EQ(root.engine_bytes, 5U * sizeof(next::Point64));
    auto moved = std::move(first);
    EXPECT_EQ(root.engine_bytes, 5U * sizeof(next::Point64));
    EXPECT_THROW(moved.emplace_back(), next::clipper_error);
    EXPECT_EQ(moved.size(), 3U);
    EXPECT_EQ(root.engine_bytes, 5U * sizeof(next::Point64));
    moved.clear();
    second.clear();
    EXPECT_EQ(root.engine_bytes, 0U);
    moved.detach();
    second.detach();
    root.maximum_workspace_bytes = 0;
    EXPECT_NO_THROW(moved.resize(10));
    EXPECT_NO_THROW(second.resize(10));
    EXPECT_EQ(root.engine_bytes, 0U);
}

TEST(Clipper2NextOffsetGeometryTests, DirectRawOwnerTransfersToFinalCategoryUntilPublication) {
    using namespace next::internal;
    const auto groups = std::vector<offset_group>{offset_group{
        next::Paths64{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}}, next::JoinType::Miter, next::EndType::Polygon}};
    constexpr auto output_bytes = 4U * sizeof(next::Point64) + sizeof(geotypes::PathDescriptor);
    auto state = offset_state{};
    for (auto limit : {std::size_t{0}, output_bytes - 1, output_bytes}) {
        auto root = offset_engine_resource_context{};
        root.maximum_staging_bytes = limit;
        auto solution = next::path_set64{};
        bool succeeded = false;
        try {
            auto owner = execute_offset_algorithm(state, groups, 5, solution, {}, nullptr, {}, &root);
            EXPECT_EQ(solution.point_count(), 4U);
            EXPECT_EQ(owner.bytes(), output_bytes);
            EXPECT_EQ(owner.category(), workspace_category::staging);
            EXPECT_EQ(root.engine_bytes, 0U);
            EXPECT_EQ(root.staging_bytes, output_bytes);
            succeeded = true;
        } catch (const next::clipper_error& error) {
            EXPECT_EQ(error.code(), next::clipper_error_code::resource_limit);
        }
        EXPECT_EQ(succeeded, limit == output_bytes);
        EXPECT_GT(root.engine_high_water, 0U);
        EXPECT_EQ(root.engine_bytes, 0U);
        EXPECT_EQ(root.staging_bytes, 0U);
    }
}

TEST(Clipper2NextOffsetGeometryTests, ParallelObserverMeasuresActualSimultaneousScratchAndRetainsChunks) {
    using namespace next::internal;
    constexpr auto raw_bytes = 32U * sizeof(next::Point64) + sizeof(geotypes::PathDescriptor);
    constexpr auto scratch_bytes = 32U * sizeof(next::Point64);
    const auto group = offset_group{next::Paths64{{{0, 0}}}, next::JoinType::Round, next::EndType::Round};
    for (bool simultaneous : {false, true}) {
        std::array<parallel_engine_workspace_slot, 2> observer_slots{};
        auto observer = parallel_engine_workspace_observer{std::span{observer_slots}};
        std::array<execution_resource_ledger, 2> roots{};
        std::array<scoped_workspace_charge, 2> charges{};
        std::array<next::path_set64, 2> outputs{};
        std::array<bool, 2> succeeded{};
        auto barrier = std::barrier{2};
        auto second_may_start = std::atomic<bool>{false};
        for (std::size_t i = 0; i < 2; ++i) {
            roots[i].parallel_observer = &observer_slots[i];
            roots[i].maximum_workspace_bytes = raw_bytes + scratch_bytes;
            roots[i].maximum_staging_bytes = 0;
            charges[i] = scoped_workspace_charge{&roots[i]};
        }
        const auto generate = [&](std::size_t i) {
            if (!simultaneous && i == 1) { second_may_start.wait(false); }
            {
                auto state = offset_state{};
                auto scope = offset_resource_scope{state, &roots[i], &charges[i]};
                try {
                    build_offset_group_paths(state, group,
                        offset_group_execution_options{.arc_segments_per_quadrant = 8}, 100, nullptr, outputs[i]);
                    succeeded[i] = true;
                } catch (...) {}
                if (simultaneous) { barrier.arrive_and_wait(); }
            }
            if (!simultaneous && i == 0) {
                second_may_start.store(true);
                second_may_start.notify_one();
            }
        };
        auto first = std::thread{generate, 0};
        auto second = std::thread{generate, 1};
        first.join();
        second.join();
        ASSERT_TRUE(succeeded[0]);
        ASSERT_TRUE(succeeded[1]);
        EXPECT_EQ(outputs[0].point_count(), 32U);
        EXPECT_EQ(outputs[1].point_count(), 32U);
        EXPECT_EQ(observer.live(), 2U * raw_bytes);
        std::printf("T09 observer simultaneous=%d peak=%zu retained=%zu\n", simultaneous,
            observer.peak(), observer.live());
        EXPECT_EQ(observer.peak(), 2U * raw_bytes + (simultaneous ? 2U : 1U) * scratch_bytes);
        charges[0].release();
        EXPECT_EQ(observer.live(), raw_bytes);
        charges[1].release();
        EXPECT_EQ(observer.live(), 0U);
    }
}

TEST(Clipper2NextOffsetGeometryTests, ObserverIgnoresRejectedRawGrowthAndRetriesWithSameChildLedger) {
    using namespace next::internal;
    std::array<parallel_engine_workspace_slot, 1> observer_slots{};
    auto observer = parallel_engine_workspace_observer{std::span{observer_slots}};
    auto child = execution_resource_ledger{};
    child.parallel_observer = &observer_slots[0];
    child.maximum_staging_bytes = 0;
    auto state = offset_state{};
    const auto group = offset_group{next::Paths64{{{0, 0}}}, next::JoinType::Round, next::EndType::Round};
    constexpr auto exact = 64U * sizeof(next::Point64) + sizeof(geotypes::PathDescriptor);
    for (const auto limit : {exact - 1U, exact}) {
        child.maximum_workspace_bytes = limit;
        bool succeeded = false;
        try {
            auto raw = next::path_set64{};
            auto owner = scoped_workspace_charge{&child};
            auto scope = offset_resource_scope{state, &child, &owner};
            build_offset_group_paths(state, group,
                offset_group_execution_options{.arc_segments_per_quadrant = 8}, 100, nullptr, raw);
            EXPECT_EQ(raw.point_count(), 32U);
            succeeded = true;
        } catch (const next::clipper_error& error) {
            EXPECT_EQ(error.code(), next::clipper_error_code::resource_limit);
        }
        EXPECT_EQ(succeeded, limit == exact);
        EXPECT_EQ(observer.peak(), limit == exact ? exact : 32U * sizeof(next::Point64));
        EXPECT_EQ(observer.live(), 0U);
        EXPECT_EQ(child.engine_bytes, 0U);
        EXPECT_EQ(child.staging_bytes, 0U);
        EXPECT_EQ(child.reserved_engine_bytes, 0U);
        EXPECT_EQ(state.path_out_charge.context(), nullptr);
    }
}

TEST(Clipper2NextOffsetGeometryTests, LedgerAndObserverPreserveFullUnsignedSizeDomain) {
    using namespace next::internal;
    constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();
    std::array<parallel_engine_workspace_slot, 1> observer_slots{};
    auto observer = parallel_engine_workspace_observer{std::span{observer_slots}};
    auto root = execution_resource_ledger{};
    root.parallel_observer = &observer_slots[0];
    root.maximum_staging_bytes = 0;
    {
        auto first = scoped_workspace_charge{&root};
        auto second = scoped_workspace_charge{&root};
        ASSERT_EQ(first.try_set_slots(maximum - 8U, 1U), next::clipper_error_code::ok);
        EXPECT_EQ(second.try_set_slots(9U, 1U), next::clipper_error_code::resource_limit);
        EXPECT_EQ(root.engine_bytes, maximum - 8U);
        EXPECT_EQ(observer.live(), maximum - 8U);
        ASSERT_EQ(second.try_set_slots(8U, 1U), next::clipper_error_code::ok);
        EXPECT_EQ(root.engine_bytes, maximum);
        EXPECT_EQ(observer.peak(), maximum);
        EXPECT_EQ(first.try_set_slots(maximum, 2U), next::clipper_error_code::resource_limit);
        EXPECT_EQ(first.try_reclassify(workspace_category::staging), next::clipper_error_code::resource_limit);
        EXPECT_EQ(root.engine_bytes, maximum);
        second.release();
        EXPECT_EQ(root.engine_bytes, maximum - 8U);
        EXPECT_EQ(observer.live(), maximum - 8U);
    }
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(observer.live(), 0U);
    root.parallel_observer = nullptr;
    ASSERT_EQ(root.try_add_work(maximum - 8U), next::clipper_error_code::ok);
    EXPECT_EQ(root.try_add_work(9U), next::clipper_error_code::resource_limit);
    ASSERT_EQ(root.try_add_work(8U), next::clipper_error_code::ok);
    EXPECT_EQ(root.try_add_work(1U), next::clipper_error_code::resource_limit);
    EXPECT_EQ(root.work, maximum);
    auto reservation = scoped_workspace_reservation{&root};
    reservation.set_bytes(maximum);
    auto owner = scoped_workspace_charge{&root};
    EXPECT_EQ(owner.try_set_slots(1U, 1U), next::clipper_error_code::resource_limit);
    EXPECT_EQ(root.reserved_engine_bytes, maximum);
    reservation.release();
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    EXPECT_EQ(root.engine_bytes, 0U);
}

TEST(Clipper2NextOffsetGeometryTests, ShrinkingGrowthKeepsItsRemainingPrefixProtected) {
    using namespace next::internal;
    for (const auto category : {workspace_category::engine, workspace_category::staging}) {
        auto root = execution_resource_ledger{};
        root.maximum_workspace_bytes = 64U;
        root.maximum_staging_bytes = 64U;
        auto owner = scoped_workspace_charge{&root, category};
        owner.set_live_slots(64U, 1U);
        {
            auto growth = scoped_workspace_growth{owner, 32U, 1U};
            growth.commit_slots(16U, 1U);
            EXPECT_EQ(owner.bytes(), 16U);
            EXPECT_EQ(category == workspace_category::engine ? root.reserved_engine_bytes
                                                             : root.reserved_staging_bytes, 16U);
            auto other = scoped_workspace_charge{&root, category};
            EXPECT_EQ(other.try_set_slots(33U, 1U), next::clipper_error_code::resource_limit);
            EXPECT_EQ(other.try_set_slots(32U, 1U), next::clipper_error_code::ok);
            growth.commit();
            EXPECT_EQ(owner.bytes(), 32U);
        }
        EXPECT_EQ(root.reserved_engine_bytes, 0U);
        EXPECT_EQ(root.reserved_staging_bytes, 0U);
        owner.release();
        EXPECT_EQ(root.engine_bytes, 0U);
        EXPECT_EQ(root.staging_bytes, 0U);
    }
}

TEST(Clipper2NextOffsetGeometryTests, ObserverPreservesExactPeakAcrossEveryOwnerInterleavingAndTransfer) {
    using namespace next::internal;
    const std::array<std::array<std::size_t, 5>, 3> values{{
        {{0, 7, 11, 3, 0}}, {{0, 5, 8, 0, 0}}, {{0, 13, 2, 29, 29}}
    }};
    const std::array<std::size_t, 3> counts{{4, 3, 4}};
    std::vector<std::size_t> order;
    std::array<std::size_t, 3> positions{};
    auto schedules = std::size_t{};
    auto observe_intermediate = false;
    const auto replay = [&] {
        std::array<parallel_engine_workspace_slot, 3> observer_slots{};
        auto observer = parallel_engine_workspace_observer{std::span{observer_slots}};
        std::array<std::size_t, 3> steps{};
        std::array<std::size_t, 3> owners{};
        auto peak = std::size_t{};
        auto correct = true;
        for (const auto worker : order) {
            const auto next_bytes = values[worker][++steps[worker]];
            observer_slots[worker].change(owners[worker], next_bytes);
            owners[worker] = next_bytes;
            const auto live = owners[0] + owners[1] + owners[2];
            peak = (std::max)(peak, live);
            if (observe_intermediate) {
                correct = correct && observer.live() == live && observer.peak() == peak;
            }
        }
        correct = correct && observer.live() == owners[0] + owners[1] + owners[2] &&
                  observer.peak() == peak;
        observer.reset_after_transfer();
        ++schedules;
        return correct && observer.live() == 0U && observer.peak() == peak;
    };
    const auto enumerate = [&](auto&& self) -> bool {
        if (order.size() == 11U) { return replay(); }
        for (std::size_t worker = 0; worker < 3; ++worker) {
            if (positions[worker] == counts[worker]) { continue; }
            ++positions[worker];
            order.push_back(worker);
            if (!self(self)) { return false; }
            order.pop_back();
            --positions[worker];
        }
        return true;
    };
    for (const auto intermediate : {false, true}) {
        observe_intermediate = intermediate;
        schedules = 0;
        ASSERT_TRUE(enumerate(enumerate));
        EXPECT_EQ(schedules, 11'550U);
    }
    std::array<parallel_engine_workspace_slot, 1> scoped_slots{};
    auto observer = parallel_engine_workspace_observer{std::span{scoped_slots}};
    for (const auto unwind : {false, true}) {
        try {
            auto publication = scoped_workspace_publication{&scoped_slots[0]};
            scoped_slots[0].change(0U, 7U);
            scoped_slots[0].change(7U, 11U);
            if (unwind) { throw 1; }
        } catch (int) {}
        EXPECT_EQ(observer.live(), 11U);
        EXPECT_EQ(observer.peak(), 11U);
        scoped_slots[0].change(11U, 0U);
    }
    constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();
    {
        auto publication = scoped_workspace_publication{&scoped_slots[0]};
        scoped_slots[0].change(0U, maximum - 8U);
        scoped_slots[0].change(maximum - 8U, maximum);
    }
    EXPECT_EQ(observer.live(), maximum);
    EXPECT_EQ(observer.peak(), maximum);
    scoped_slots[0].change(maximum, 0U);
    {
        auto publication = scoped_workspace_publication{nullptr};
        publication.release();
    }
    // A reader may observe a completed prefix while a long growth phase
    // continues. It must not depend on that phase ending to make progress.
    std::atomic<bool> requested{}, observed{};
    std::thread reader;
    auto current = std::size_t{};
    auto sampled = std::size_t{};
    auto completed_inside_phase = false;
    {
        auto publication = scoped_workspace_publication{&scoped_slots[0]};
        scoped_slots[0].change(0U, ++current);
        reader = std::thread{[&] {
            requested.store(true, std::memory_order_release);
            requested.notify_one();
            sampled = observer.live();
            observed.store(true, std::memory_order_release);
        }};
        requested.wait(false, std::memory_order_acquire);
        for (auto step = 0U; step < 1'000U && !observed.load(std::memory_order_acquire); ++step) {
            scoped_slots[0].change(current, current + 1U);
            ++current;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        completed_inside_phase = observed.load(std::memory_order_acquire);
    }
    reader.join();
    EXPECT_TRUE(completed_inside_phase);
    EXPECT_GE(sampled, 1U);
    EXPECT_LE(sampled, current);
    EXPECT_EQ(observer.live(), current);
    EXPECT_EQ(observer.peak(), maximum);
    scoped_slots[0].change(current, 0U);
    {
        static_assert(sizeof(parallel_engine_workspace_slot) == 88U);
        static_assert(sizeof(parallel_engine_workspace_observer) == 32U);
        std::array<parallel_engine_workspace_slot, 2> concurrent_slots{};
        parallel_engine_workspace_observer concurrent{concurrent_slots};
        auto start = std::barrier{3};
        auto phase_end = std::barrier{2};
        std::atomic<unsigned> done{}, queries{};
        const auto writer = [&](unsigned id) {
            start.arrive_and_wait();
            for (auto cycle = 0U; cycle < 64U; ++cycle) {
                auto actual = std::size_t{};
                try {
                    auto publication = scoped_workspace_publication{&concurrent_slots[id]};
                    for (auto point = 0U; point < 256U; ++point) {
                        concurrent_slots[id].change(actual, actual + 1U);
                        ++actual;
                        if (id == 0U && cycle == 31U && actual == 128U) { throw 1; }
                    }
                } catch (int) {}
                phase_end.arrive_and_wait();
                concurrent_slots[id].change(actual, 0U);
                phase_end.arrive_and_wait();
            }
            done.fetch_add(1U, std::memory_order_release);
        };
        auto first = std::thread{writer, 0U};
        auto second = std::thread{writer, 1U};
        auto reader = std::thread{[&] {
            start.arrive_and_wait();
            do {
                static_cast<void>(concurrent.live());
                queries.fetch_add(1U, std::memory_order_relaxed);
            } while (done.load(std::memory_order_acquire) != 2U);
        }};
        first.join();
        second.join();
        reader.join();
        EXPECT_NE(queries.load(), 0U);
        EXPECT_EQ(concurrent.live(), 0U);
        EXPECT_EQ(concurrent.peak(), 512U);
    }
}


TEST(Clipper2NextOffsetGeometryTests, FrameConstructorQuotaFailureReleasesAllBoundChildren) {
    using namespace next::internal;
    auto paths = next::Paths64{};
    for (int index = 0; index < 64; ++index) {
        paths.push_back({{index * 100, 0}, {index * 100 + 20, 0}, {index * 100, 20}});
    }
    const auto group = offset_group{std::move(paths), next::JoinType::Miter, next::EndType::Polygon};
    std::array<offset_path_chunk, 64> chunks{};
    for (std::size_t index = 0; index < chunks.size(); ++index) { chunks[index] = {index, index + 1U}; }
    auto root = execution_resource_ledger{};
    root.maximum_workspace_bytes = 0U;
    auto rejected = false;
    try {
        auto frame = offset_parallel_results{&root, group, {}, 5.0, chunks, 16U};
    } catch (const next::clipper_error& error) {
        EXPECT_EQ(error.code(), next::clipper_error_code::resource_limit);
        rejected = true;
    }
    EXPECT_TRUE(rejected);
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    EXPECT_EQ(root.reserved_staging_bytes, 0U);
}

TEST(Clipper2NextOffsetGeometryTests, GeneratedPointAdmissionMatchesWorkingDomainAndRounding) {
    const auto saved = std::fegetround();
    ASSERT_NE(saved, -1);
    struct restore_rounding final {
        int mode;
        ~restore_rounding() { std::fesetround(mode); }
    } restore{saved};
    next::Path64 reserved_output;
    reserved_output.reserve(1U);
    const next::Path64 unreserved_output;
    const auto infinity = std::numeric_limits<double>::infinity();
    const std::array values{0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5,
        0x1p61, -0x1p61, std::nextafter(0x1p61, 0.0), std::nextafter(-0x1p61, 0.0),
        std::nextafter(0x1p61, infinity), std::nextafter(-0x1p61, -infinity),
        0x1p63, -0x1p63, infinity, -infinity, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::denorm_min()};
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        for (const auto rounding : {geotypes::CoordinateRounding::NearestEven,
             geotypes::CoordinateRounding::NearestAwayFromZero,
             static_cast<geotypes::CoordinateRounding>(-1)}) {
            for (const auto x : values) {
                for (const auto y : values) {
                    const next::PointD point{x, y};
                    const auto generic = geotypes::checkedPointCast<std::int64_t>(point, rounding);
                    const auto accepted = generic && generic->x >= next::MIN_COORD &&
                        generic->x <= next::MAX_COORD && generic->y >= next::MIN_COORD &&
                        generic->y <= next::MAX_COORD;
                    const std::array policies{
                        next::internal::offset_point_rounding{rounding},
                        next::internal::offset_point_rounding::for_reserved_phase(
                            rounding, reserved_output, 1U),
                        next::internal::offset_point_rounding::for_reserved_phase(
                            rounding, unreserved_output, 1U)};
                    for (const auto& policy : policies) {
                        if (accepted) {
                            EXPECT_EQ(next::internal::offset_point(point, policy), *generic);
                        } else {
                            try {
                                static_cast<void>(next::internal::offset_point(point, policy));
                                ADD_FAILURE() << "out-of-domain point accepted";
                            } catch (const next::clipper_error& error) {
                                EXPECT_EQ(error.code(), next::clipper_error_code::coordinate_range);
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST(Clipper2NextOffsetGeometryTests, ContiguousPointAppendKeepsQuotaFailurePrefixAndEmptyCharges) {
    using namespace next::internal;
    execution_resource_ledger root;
    engine_buffer<next::Point64> points;
    const std::array<next::Point64, 4> source{{{1, 2}, {3, 5}, {7, 11}, {13, 17}}};
    points.reserve(source.size());
    points.bind(&root);
    root.maximum_workspace_bytes = 2U * sizeof(next::Point64);
    bool limited = false;
    try { points.append_range(std::span<const next::Point64>{source}); }
    catch (const next::clipper_error& error) { limited = error.code() == next::clipper_error_code::resource_limit; }
    ASSERT_TRUE(limited);
    ASSERT_EQ(points.size(), 2U);
    EXPECT_EQ(points[0], source[0]);
    EXPECT_EQ(points[1], source[1]);
    EXPECT_EQ(root.engine_bytes, 2U * sizeof(next::Point64));
    EXPECT_EQ(root.engine_high_water, root.engine_bytes);
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    points.clear();
    root.maximum_workspace_bytes = 0U;
    points.append_range({});
    EXPECT_TRUE(points.empty());
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
}

TEST(Clipper2NextOffsetGeometryTests, TrimContiguousRunsRetainsExactOrderAndPrePruningPeak) {
    using namespace next::internal;
    const next::Path64 source{{1, 0}, {-2, 2}, {1, 0}, {-1, 2}, {-2, 2}, {1, 1}, {1, 2}, {0, 0}, {1, 2}};
    const next::Path64 expected{{1, 0}, {-1, 2}, {-2, 2}, {1, 1}};
    const auto in_range = [](const next::Point64& a, const next::Point64& b,
                              const next::Point64& c) {
        return cross_product_sign_in_clipper_range(a, b, c) == 0;
    };
    for (const bool validated : {false, true}) {
        execution_resource_ledger root;
        {
            engine_buffer<next::Point64> result;
            result.bind(&root);
            if (validated) { trim_collinear_into(source, false, result, in_range); }
            else { trim_collinear_into(source, false, result); }
            EXPECT_TRUE(std::ranges::equal(result, expected));
            EXPECT_EQ(root.engine_bytes, expected.size() * sizeof(next::Point64));
            // Five points are effective before the original end-pruning removes one.
            EXPECT_EQ(root.engine_high_water, 5U * sizeof(next::Point64));
            EXPECT_EQ(root.reserved_engine_bytes, 0U);
        }
        EXPECT_EQ(root.engine_bytes, 0U);
    }
    const std::array<std::int64_t, 9> coordinates{
        next::MIN_COORD, next::MIN_COORD + 1, -(1LL << 27) - 1, -1, 0,
        1, (1LL << 27) + 1, next::MAX_COORD - 1, next::MAX_COORD};
    next::Path64 points;
    for (const auto x : coordinates) {
        for (const auto y : coordinates) { points.emplace_back(x, y); }
    }
    for (const auto& a : points) {
        for (const auto& b : points) {
            for (const auto& c : points) {
                ASSERT_EQ(in_range(a, b, c), cross_product_sign(a, b, c) == 0);
            }
        }
    }
}


TEST(Clipper2NextOffsetGeometryTests, ReleasedScratchScopePreservesTheNextBindingAndRetainedCapacity) {
    using namespace next::internal;
    execution_resource_ledger first;
    execution_resource_ledger second;
    scoped_workspace_charge first_output{&first};
    scoped_workspace_charge second_output{&second};
    offset_state state;
    const next::Path64 square{{0,0},{16,0},{16,16},{0,16}};
    const auto bytes = square.size() * sizeof(next::PointD);
    {
        auto scope = offset_resource_scope{state,&first,&first_output};
        state.construct_normals(square);
        ASSERT_EQ(first.engine_bytes,bytes);
        const auto capacity = state.normals.capacity();
        scope.release();
        EXPECT_EQ(first.engine_bytes,0U);
        EXPECT_EQ(first.reserved_engine_bytes,0U);
        EXPECT_TRUE(state.normals.empty());
        EXPECT_EQ(state.normals.capacity(),capacity);
        EXPECT_EQ(state.normals_charge.context(),nullptr);
        EXPECT_EQ(state.output_charge,nullptr);

        state.bind(&second,&second_output);
        state.construct_normals(square);
        scope.release();
        EXPECT_EQ(second.engine_bytes,bytes);
        EXPECT_EQ(state.normals.size(),square.size());
    }
    EXPECT_EQ(second.engine_bytes,bytes);
    EXPECT_EQ(state.normals.size(),square.size());
    state.reset();
    state.detach();
    EXPECT_EQ(second.engine_bytes,0U);
}


TEST(Clipper2NextOffsetGeometryTests, ScratchPhaseClearPreservesForeignQuotaOutputAndExactPeak) {
    using namespace next::internal;
    std::array<parallel_engine_workspace_slot, 2> slots;
    parallel_engine_workspace_observer observer{slots};
    execution_resource_ledger root, other_root;
    root.parallel_observer = &slots[0];
    other_root.parallel_observer = &slots[1];
    auto foreign = scoped_workspace_charge{&root};
    auto other = scoped_workspace_charge{&other_root};
    auto output = scoped_workspace_charge{&root, workspace_category::staging};
    auto quota = scoped_workspace_reservation{&root};
    auto staging_quota = scoped_workspace_reservation{&root, workspace_category::staging};
    foreign.set_live_slots(4U, 1U);
    other.set_live_slots(28U, 1U);
    output.set_live_slots(7U, 1U);
    ASSERT_EQ(quota.try_set_bytes(11U), next::clipper_error_code::ok);
    ASSERT_EQ(staging_quota.try_set_bytes(13U), next::clipper_error_code::ok);
    root.maximum_workspace_bytes = 111U;
    root.maximum_staging_bytes = 20U;
    offset_state state;
    state.bind(&root, &output);
    const auto grow = [](auto& values, auto& charge, std::size_t size) {
        auto growth = scoped_workspace_growth{charge, size, 16U};
        values.resize(size);
        growth.commit();
    };
    grow(state.normals, state.normals_charge, 2U);
    grow(state.path_out, state.path_out_charge, 3U);
    grow(state.callback_path, state.callback_charge, 1U);
    const auto capacities = std::array{
        state.normals.capacity(), state.path_out.capacity(), state.callback_path.capacity()};
    state.clear_scratch();
    EXPECT_TRUE(state.normals.empty() && state.path_out.empty() && state.callback_path.empty());
    EXPECT_EQ((std::array{state.normals.capacity(), state.path_out.capacity(), state.callback_path.capacity()}), capacities);
    EXPECT_EQ(root.engine_bytes, 4U);
    EXPECT_EQ(root.engine_high_water, 100U);
    EXPECT_EQ(root.reserved_engine_bytes, 11U);
    EXPECT_EQ(root.staging_bytes, 7U);
    EXPECT_EQ(root.reserved_staging_bytes, 13U);
    EXPECT_EQ(state.output_charge, &output);
    EXPECT_EQ(state.normals_charge.context(), &root);
    EXPECT_EQ(state.path_out_charge.bytes(), 0U);
    EXPECT_EQ(observer.live(), 32U);
    EXPECT_EQ(observer.peak(), 128U);
    state.clear_scratch();
    EXPECT_EQ(observer.live(), 32U);
    EXPECT_EQ(observer.peak(), 128U);
    state.detach();
}
