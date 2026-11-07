#include <gtest/gtest.h>

#include "clipper2next/clipper.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_algorithm_support.h"
#include "offset/private/offset_fast_path.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_state.h"
#include "support/test_paths.h"

#include <cstddef>
#include <vector>

namespace next = clipper2next;
namespace test = clipper2next::tests;

namespace {

[[nodiscard]] auto many_disjoint_offset_inputs(std::size_t count) -> next::Paths64 {
    next::Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto left = static_cast<int64_t>(index * 1000U);
        paths.push_back(test::path64({left, 0, left + 100, 0, left + 100, 100, left, 100}));
    }
    return paths;
}

}  // namespace



TEST(Clipper2NextOffsetFastPathTests, InflatePathsFastPathUsesCallerOwnedState) {
    next::internal::offset_state state;
    state.normals.reserve(64);
    const auto reserved_capacity = state.normals.capacity();
    const auto paths = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::internal::inflate_paths_with_state(
        state, paths, 10.0, next::JoinType::Miter, next::EndType::Polygon, 2.0, 0.0);

    ASSERT_EQ(result.size(), 1U);
    EXPECT_GE(state.normals.capacity(), reserved_capacity);
}

TEST(Clipper2NextOffsetFastPathTests, ReversedSimpleRawOffsetCanReturnDirectly) {
    next::Paths64 input{
        test::path64({0, 100, 100, 100, 100, 0, 0, 0}),
    };
    ASSERT_FALSE(next::is_positive(input.front()));

    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(input, next::JoinType::Miter, next::EndType::Polygon);
    ASSERT_TRUE(groups.front().is_reversed);

    next::internal::offset_algorithm_options options;
    next::Paths64 solution = input;

    EXPECT_TRUE(next::internal::can_return_direct_simple_offset(
        groups, solution, 10.0, nullptr, options, true));

    next::internal::canonicalize_direct_offset_solution(solution, true);
    ASSERT_EQ(solution.size(), 1U);
    EXPECT_TRUE(next::is_positive(solution.front()));
}

TEST(Clipper2NextOffsetFastPathTests, ManyDisjointSimpleRawOffsetsCanReturnDirectly) {
    next::Paths64 solution;
    constexpr int path_count = 24;
    for (int index = 0; index < path_count; ++index) {
        const auto left = static_cast<int64_t>(index * 100);
        solution.push_back(test::path64({left, 0, left + 10, 0, left + 10, 10, left, 10}));
    }

    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(solution, next::JoinType::Miter, next::EndType::Polygon);

    next::internal::offset_algorithm_options options;
    EXPECT_TRUE(next::internal::can_return_direct_disjoint_simple_offset(
        groups, solution, 10.0, nullptr, options, false));
}

TEST(Clipper2NextOffsetFastPathTests, TouchingRawOffsetBoundsCannotReturnDirectlyAsDisjoint) {
    next::Paths64 solution{
        test::path64({0, 0, 10, 0, 10, 10, 0, 10}),
        test::path64({10, 10, 20, 10, 20, 20, 10, 20}),
    };
    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(solution, next::JoinType::Miter, next::EndType::Polygon);

    next::internal::offset_algorithm_options options;
    EXPECT_FALSE(next::internal::can_return_direct_disjoint_simple_offset(
        groups, solution, 10.0, nullptr, options, false));
}

TEST(Clipper2NextOffsetFastPathTests, ManyStrictlyDisjointRawOffsetsReturnDirectly) {
    constexpr std::size_t path_count = 80U;
    auto input = many_disjoint_offset_inputs(path_count);
    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(input, next::JoinType::Miter, next::EndType::Polygon);
    ASSERT_TRUE(next::internal::can_return_direct_disjoint_simple_offset(
        groups, input, 10.0, nullptr, {}, false));

    next::internal::offset_state state;
    next::internal::offset_algorithm_options options;
    next::Paths64 solution;

    next::internal::execute_offset_algorithm(
        state, groups, 10.0, solution, nullptr, options, nullptr);

    ASSERT_EQ(solution.size(), path_count);
    for (const auto& path : solution) {
        EXPECT_TRUE(next::is_positive(path));
        EXPECT_EQ(path.size(), 4U);
    }
}

TEST(Clipper2NextOffsetFastPathTests, DisjointSimpleRawOffsetLimitAcceptsConfiguredUpperBound) {
    auto input = many_disjoint_offset_inputs(256U);
    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(input, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_TRUE(next::internal::can_return_direct_disjoint_simple_offset(
        groups, input, 10.0, nullptr, {}, false));
}

TEST(Clipper2NextOffsetFastPathTests, DisjointSimpleRawOffsetLimitRejectsAboveConfiguredUpperBound) {
    auto input = many_disjoint_offset_inputs(257U);
    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(input, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_FALSE(next::internal::can_return_direct_disjoint_simple_offset(
        groups, input, 10.0, nullptr, {}, false));
}


TEST(Clipper2NextOffsetFastPathTests, FlatCertificatePreservesAlreadySimpleDisjointPoints) {
    const next::Paths64 input{
        {{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}},
        {{30, 0}, {35, 0}, {40, 0}, {40, 10}, {30, 10}},
    };
    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(input, next::JoinType::Miter, next::EndType::Polygon);
    next::path_set64 flat;
    for (const auto& path : input) { flat.append(path, geotypes::PathClosure::ClosedImplicit); }
    const auto points = std::vector<next::Point64>(flat.points().begin(), flat.points().end());
    ASSERT_TRUE(next::internal::try_prepare_direct_disjoint_simple_offset(
        groups, flat, 10.0, {}, false));
    EXPECT_EQ(flat.size(), input.size());
    EXPECT_EQ(std::vector<next::Point64>(flat.points().begin(), flat.points().end()), points);
}

TEST(Clipper2NextOffsetFastPathTests, FlatCertificateOverlappingSimpleBoundsNeedNoPreparationStorage) {
    const next::Paths64 input{
        {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        {{5, 5}, {15, 5}, {15, 15}, {5, 15}},
    };
    std::vector<next::internal::offset_group> groups;
    groups.emplace_back(input, next::JoinType::Miter, next::EndType::Polygon);
    next::path_set64 flat;
    for (const auto& path : input) { flat.append(path, geotypes::PathClosure::ClosedImplicit); }
    const auto points = std::vector<next::Point64>(flat.points().begin(), flat.points().end());
    next::internal::execution_resource_ledger resources;
    next::internal::scoped_workspace_charge raw{&resources};
    const auto raw_bytes = flat.point_count() * sizeof(next::Point64) +
        flat.size() * sizeof(geotypes::PathDescriptor);
    raw.set_live_slots(raw_bytes, 1U);
    resources.maximum_workspace_bytes = raw_bytes + flat.size() * sizeof(next::Rect64);
    bool direct = true;
    EXPECT_NO_THROW(direct = next::internal::try_prepare_direct_disjoint_simple_offset(
        groups, flat, 10.0, {}, false, &raw));
    EXPECT_FALSE(direct);
    EXPECT_EQ(raw.bytes(), raw_bytes);
    EXPECT_EQ(resources.engine_bytes, raw_bytes);
    EXPECT_EQ(resources.engine_high_water, resources.maximum_workspace_bytes);
    EXPECT_EQ(resources.reserved_engine_bytes, 0U);
    EXPECT_EQ(std::vector<next::Point64>(flat.points().begin(), flat.points().end()), points);
}
