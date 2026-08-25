#include <gtest/gtest.h>

#include "clipper2next/clipper.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_group_processor.h"
#include "support/test_paths.h"

namespace next = clipper2next;
namespace test = clipper2next::tests;

namespace {

[[nodiscard]] auto make_batched_paths(
    const int path_count,
    const int vertices_per_side) -> next::Paths64 {
    next::Paths64 paths;
    paths.reserve(static_cast<std::size_t>(path_count));
    for (int index = 0; index < path_count; ++index) {
        next::Path64 path;
        path.reserve(static_cast<std::size_t>(vertices_per_side) * 4U);
        const auto origin_x = static_cast<int64_t>((index % 12) * 2000);
        const auto origin_y = static_cast<int64_t>((index / 12) * 2000);
        const auto extent =
            static_cast<int64_t>((vertices_per_side - 1) * 8);
        for (int vertex = 0; vertex < vertices_per_side * 4; ++vertex) {
            const auto side = vertex % vertices_per_side;
            const auto step = static_cast<int64_t>(side * 8);
            if (vertex < vertices_per_side) {
                path.emplace_back(origin_x + step, origin_y);
            } else if (vertex < vertices_per_side * 2) {
                path.emplace_back(origin_x + extent, origin_y + step);
            } else if (vertex < vertices_per_side * 3) {
                path.emplace_back(
                    origin_x + extent - step, origin_y + extent);
            } else {
                path.emplace_back(origin_x, origin_y + extent - step);
            }
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

}  // namespace

TEST(Clipper2NextOffsetGroupTests, OffsetGroupOwnsCleanedInputPaths) {
    const next::Path64 path{{0, 0}, {10, 0}, {10, 0}, {0, 10}, {0, 0}};

    const next::internal::offset_group group{
        next::Paths64{path}, next::JoinType::Miter, next::EndType::Polygon};

    ASSERT_EQ(group.path_count(), 1U);
    EXPECT_EQ(group.path(0U).size(), 3U);
    EXPECT_TRUE(group.lowest_path_index.has_value());
}

TEST(Clipper2NextOffsetGroupTests, OffsetPathResultMatchesRecordedPolygonGolden) {
    next::internal::offset_state state;
    state.delta = 2.0;
    next::internal::offset_group group{next::Paths64{test::path64({0, 0, 20, 0, 20, 20, 0, 20})},
                                       next::JoinType::Miter,
                                       next::EndType::Polygon};

    const auto result = next::internal::build_offset_path_result(
        state,
        group,
        group.path(0U),
        next::internal::offset_group_execution_options{2.0, 0.0, 0U, false},
        nullptr);

    const next::Paths64 expected{
        test::path64({-2, -2, 22, -2, 22, 22, -2, 22}),
    };
    EXPECT_EQ(result.paths, expected);
}

TEST(Clipper2NextOffsetGroupTests, OffsetPathResultMatchesRecordedOpenAndDegenerateGoldens) {
    {
        next::internal::offset_state state;
        state.delta = 2.0;
        next::internal::offset_group group{next::Paths64{test::path64({0, 0, 20, 0, 20, 20})},
                                           next::JoinType::Miter,
                                           next::EndType::Joined};
        const auto result = next::internal::build_offset_path_result(
            state,
            group,
            group.path(0U),
            next::internal::offset_group_execution_options{2.0, 0.0, 0U, false},
            nullptr);
        const next::Paths64 expected{
            test::path64({-2, 0, -1, -2, 22, -2, 22, 21, 20, 22}),
            test::path64({21, 19, 20, 20, 18, 20, 18, 0, 20, 0, 20, 2, 0, 2, 0, 0, 1, -1}),
        };
        EXPECT_EQ(result.paths, expected);
    }
    {
        next::internal::offset_state state;
        state.delta = 2.0;
        next::internal::offset_group group{
            next::Paths64{test::path64({5, 5})}, next::JoinType::Miter, next::EndType::Polygon};
        const auto result = next::internal::build_offset_path_result(
            state,
            group,
            group.path(0U),
            next::internal::offset_group_execution_options{2.0, 0.0, 0U, false},
            nullptr);
        const next::Paths64 expected{
            test::path64({3, 3, 7, 3, 7, 7, 3, 7}),
        };
        EXPECT_EQ(result.paths, expected);
    }
    {
        next::internal::offset_state state;
        state.delta = 2.0;
        next::internal::offset_group group{next::Paths64{test::path64({0, 0, 20, 0})},
                                           next::JoinType::Miter,
                                           next::EndType::Joined};
        const auto result = next::internal::build_offset_path_result(
            state,
            group,
            group.path(0U),
            next::internal::offset_group_execution_options{2.0, 0.0, 0U, false},
            nullptr);
        const next::Paths64 expected{
            test::path64({-2, 2, -2, -2, 22, -2, 22, 2}),
        };
        EXPECT_EQ(result.paths, expected);
    }
}

TEST(Clipper2NextOffsetGroupTests, OffsetPathResultMatchesRecordedRoundJoinGolden) {
    next::internal::offset_state state;
    state.delta = 3.0;
    next::internal::offset_group group{next::Paths64{test::path64({0, 0, 20, 0, 20, 20, 0, 20})},
                                       next::JoinType::Round,
                                       next::EndType::Polygon};

    const auto result = next::internal::build_offset_path_result(
        state,
        group,
        group.path(0U),
        next::internal::offset_group_execution_options{2.0, 1.0, 0U, false},
        nullptr);

    const next::Paths64 expected{
        test::path64({-3, 0, 0, -3, 20, -3, 23, 0, 23, 20, 20, 23, 0, 23, -3, 20}),
    };
    EXPECT_EQ(result.paths, expected);
}

TEST(Clipper2NextOffsetGroupTests, OffsetGroupParallelPathMatchesSequentialOutput) {
    const auto paths = make_batched_paths(96, 48);
    const next::internal::offset_group group{paths, next::JoinType::Miter, next::EndType::Polygon};
    const next::internal::offset_group_execution_options options{2.0, 0.0, false};

    next::internal::offset_state sequential_state;
    sequential_state.delta = 2.0;
    next::Paths64 sequential_output;
    next::internal::build_offset_group_paths(
        sequential_state, group, options, nullptr, sequential_output);

    next::internal::offset_state parallel_state;
    parallel_state.delta = 2.0;
    next::Paths64 parallel_output;
    next::internal::build_offset_group_paths_parallel(
        parallel_state, group, options, 2.0, {}, parallel_output);

    EXPECT_EQ(parallel_output, sequential_output);
}

TEST(Clipper2NextOffsetGroupTests, OffsetGroupParallelEligibilityRejectsSmallProductWorkloads) {
    next::Paths64 paths;
    paths.reserve(64);
    for (int index = 0; index < 64; ++index) {
        const auto x = static_cast<int64_t>((index % 10) * 140);
        const auto y = static_cast<int64_t>((index / 10) * 125);
        paths.push_back(test::path64({x,
                                      y,
                                      x + 70,
                                      y - 12,
                                      x + 112,
                                      y + 38,
                                      x + 74,
                                      y + 98,
                                      x + 10,
                                      y + 86,
                                      x - 24,
                                      y + 28}));
    }

    const next::internal::offset_group group{paths, next::JoinType::Miter, next::EndType::Polygon};

    EXPECT_FALSE(next::internal::is_offset_group_parallel_eligible(group));
}

TEST(Clipper2NextOffsetGroupTests, OffsetGroupParallelEligibilityAcceptsLargeBatchedWorkloads) {
    const auto paths = make_batched_paths(512, 257);
    const next::internal::offset_group group{paths, next::JoinType::Miter, next::EndType::Polygon};

    EXPECT_TRUE(next::internal::is_offset_group_parallel_eligible(group));
}
