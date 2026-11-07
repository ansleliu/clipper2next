#include <array>
#include <limits>
#include <optional>
#include "clipper2next/clipper.h"
#include "offset/private/offset_output.h"
#include "offset/private/offset_executor.h"
#include "geometry/private/numeric_policy.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetOutputTests, AppendPerpendicularAddsRoundedOffsetPoint) {
    next::Path64 output;

    next::internal::append_perpendicular(output, {10, 20}, {1.0, 0.0}, 2.5);

    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0], next::Point64(12, 20));
    using namespace next::internal;
    for (const auto limit : std::array<std::size_t, 7>{0U, 15U, 16U, 31U, 63U, 64U,
             (std::numeric_limits<std::size_t>::max)()}) {
        auto root = execution_resource_ledger{};
        root.maximum_workspace_bytes = limit;
        auto owner = scoped_workspace_charge{&root};
        auto points = next::Path64{};
        points.reserve(4U);
        bool limited = false;
        try {
            auto phase = scoped_workspace_growth{&owner};
            auto* shared = phase.try_reserve_slots(4U, sizeof(next::Point64)) ==
                    next::clipper_error_code::ok ? &phase : nullptr;
            append_perpendicular(points, {10, 20}, {1.0, 0.0}, 2.5,
                                 offset_point_rounding{}, &owner, shared);
        } catch (const next::clipper_error& error) {
            EXPECT_EQ(error.code(), next::clipper_error_code::resource_limit);
            limited = true;
        }
        EXPECT_EQ(limited, limit < sizeof(next::Point64));
        EXPECT_EQ(points.size(), limited ? 0U : 1U);
        if (!limited) { EXPECT_EQ(points[0], next::Point64(12, 20)); }
        EXPECT_EQ(root.engine_bytes, points.size() * sizeof(next::Point64));
        EXPECT_EQ(root.engine_high_water, root.engine_bytes);
        EXPECT_EQ(root.reserved_engine_bytes, 0U);
        points.clear();
        owner.release();
        EXPECT_EQ(root.engine_bytes, 0U);
    }
}

TEST(Clipper2NextOffsetOutputTests, AppendMiterAddsSingleCornerPoint) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    next::Path64 output;

    next::internal::append_miter(output, path, normals, 1, 0, 2.0, 0.0);

    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0], next::Point64(12, -2));
}

TEST(Clipper2NextOffsetOutputTests, AppendBevelAddsEdgeOffsetPair) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    next::Path64 output;

    next::internal::append_bevel(output, path, normals, 1, 0, 2.0);

    ASSERT_EQ(output.size(), 2U);
    EXPECT_EQ(output[0], next::Point64(10, -2));
    EXPECT_EQ(output[1], next::Point64(12, 0));
}

TEST(Clipper2NextOffsetOutputTests, AppendSquareAddsReflectedCornerPair) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    next::Path64 output;

    next::internal::append_square(output, path, normals, 1, 0, 2.0);

    ASSERT_EQ(output.size(), 2U);
    EXPECT_EQ(output[0], next::Point64(11, -2));
    EXPECT_EQ(output[1], next::Point64(12, -1));
}

TEST(Clipper2NextOffsetOutputTests, AppendRoundUsesLegacyArcParameters) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10});
    const next::PathD normals{{0.0, -1.0}, {1.0, 0.0}, {-0.7071067811865475, 0.7071067811865475}};
    const auto arc = next::internal::make_arc_parameters(2.0, 0.0);
    next::Path64 output;

    next::internal::append_round(output, path, normals, 1, 0, 2.0, arc, next::internal::pi / 2);

    ASSERT_GE(output.size(), 3U);
    EXPECT_EQ(output.front(), next::Point64(10, -2));
    EXPECT_EQ(output.back(), next::Point64(12, 0));
}

TEST(Clipper2NextOffsetOutputTests, ReserveEstimateCoversPolygonAndOpenPaths) {
    const auto arc = next::internal::make_arc_parameters(10.0, 0.0);

    EXPECT_EQ(next::internal::estimate_path_output_capacity(
                  4, next::JoinType::Miter, next::EndType::Polygon, arc, 10.0),
              12U);
    EXPECT_GE(next::internal::estimate_path_output_capacity(
                  4, next::JoinType::Round, next::EndType::Polygon, arc, 10.0),
              4U);
    EXPECT_EQ(next::internal::estimate_path_output_capacity(
                  4, next::JoinType::Bevel, next::EndType::Butt, arc, 10.0),
              16U);
    EXPECT_EQ(next::internal::estimate_single_point_output_capacity(next::JoinType::Square, arc, 10.0),
              4U);
    EXPECT_GE(next::internal::estimate_single_point_output_capacity(next::JoinType::Round, arc, 10.0),
              1U);
}

#include "support/private/engine_resource_plan.h"
TEST(Clipper2NextOffsetOutputTests, PartialCoordinateFailureCountsOnlyConstructedPoints) {
    using namespace next::internal;
    const auto input = next::Path64{{next::MAX_COORD - 1024, 0}, {next::MAX_COORD - 1024, 0}};
    const auto normals = next::PathD{{-1, 0}, {1, 0}};
    const auto arc = make_arc_parameters(2048, 0, 8);
    for (const auto phased : {false, true}) {
      for (int kind = 0; kind < 3; ++kind) {
        std::array<parallel_engine_workspace_slot, 1> observer_slots{};
        auto observer = parallel_engine_workspace_observer{std::span{observer_slots}};
        auto root = execution_resource_ledger{};
        root.parallel_observer = &observer_slots[0];
        auto output = next::Path64{};
        output.reserve(64U);
        auto owner = scoped_workspace_charge{&root};
        bool failed = false;
        try {
            auto phase = std::optional<scoped_workspace_growth>{};
            if (phased) {
                phase.emplace(&owner);
                ASSERT_EQ(phase->try_reserve_slots(64U, sizeof(next::Point64)),
                          next::clipper_error_code::ok);
            }
            auto* shared = phase ? &*phase : nullptr;
            const auto rounding = offset_point_rounding{geotypes::CoordinateRounding::NearestEven};
            if (kind == 0) { append_bevel(output, input, normals, 1, 0, 2048, rounding, &owner, shared); }
            else if (kind == 1) { append_square(output, input, normals, 1, 1, 2048, rounding, &owner, shared); }
            else { append_round(output, input, normals, 1, 1, 2048, arc, pi, rounding, &owner, shared); }
        } catch (const next::clipper_error& error) {
            failed = true;
            EXPECT_EQ(error.code(), next::clipper_error_code::coordinate_range);
        }
        EXPECT_TRUE(failed);
        ASSERT_EQ(output.size(), kind == 2 ? 11U : 1U);
        const auto actual = output.size() * sizeof(next::Point64);
        EXPECT_EQ(root.engine_bytes, actual);
        EXPECT_EQ(root.engine_high_water, actual);
        EXPECT_EQ(observer.live(), actual);
        EXPECT_EQ(observer.peak(), actual);
        EXPECT_EQ(root.reserved_engine_bytes, 0U);
        output.clear();
        owner.release();
        EXPECT_EQ(root.engine_bytes, 0U);
        EXPECT_EQ(observer.live(), 0U);
      }
    }
}
