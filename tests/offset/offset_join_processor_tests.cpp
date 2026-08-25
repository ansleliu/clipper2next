#include <gtest/gtest.h>

#include "clipper2next/clipper.h"
#include "offset/private/offset_geometry.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_join_processor.h"
#include "support/test_paths.h"

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetJoinProcessorTests, EmitsConcaveVertexBridge) {
    const auto path = test::path64({0, 0, 10, 0, 10, 10, 5, 5, 0, 10});
    next::internal::offset_state state;
    next::internal::assign_normals(state.normals, path);
    state.group_delta = 2.0;
    next::internal::offset_group group{
        next::Paths64{path}, next::JoinType::Miter, next::EndType::Polygon};

    next::internal::append_offset_join(
        state,
        group,
        path,
        path,
        3,
        2,
        next::internal::offset_join_options{next::JoinType::Miter, 0.0, 0U},
        nullptr);

    ASSERT_EQ(state.path_out.size(), 3U);
    EXPECT_EQ(state.path_out[1], path[3]);
}

TEST(Clipper2NextOffsetJoinProcessorTests, MitersAlmostStraightNonRoundJoin) {
    const auto path = test::path64({0, 0, 1000, 0, 2000, 1, 3000, 1});
    next::internal::offset_state state;
    next::internal::assign_normals(state.normals, path);
    state.group_delta = 2.0;
    next::internal::offset_group group{
        next::Paths64{path}, next::JoinType::Bevel, next::EndType::Polygon};

    next::internal::append_offset_join(
        state,
        group,
        path,
        path,
        1,
        0,
        next::internal::offset_join_options{next::JoinType::Bevel, 0.0, 0U},
        nullptr);

    EXPECT_EQ(state.path_out.size(), 1U);
}

TEST(Clipper2NextOffsetJoinProcessorTests, RoundJoinUsesArcParameters) {
    const auto path = test::path64({0, 0, 10, 0, 10, -10, 0, -10});
    next::internal::offset_state state;
    next::internal::assign_normals(state.normals, path);
    state.group_delta = 10.0;
    state.arc = next::internal::make_arc_parameters(state.group_delta, 0.0);
    next::internal::offset_group group{
        next::Paths64{path}, next::JoinType::Round, next::EndType::Polygon};

    next::internal::append_offset_join(
        state,
        group,
        path,
        path,
        1,
        0,
        next::internal::offset_join_options{next::JoinType::Round, 0.0, 0U},
        nullptr);

    EXPECT_GT(state.arc.steps_per_rad, 0.0);
    EXPECT_GT(state.path_out.size(), 2U);
}

TEST(Clipper2NextOffsetJoinProcessorTests, DeltaCallbackOverridesGroupDeltaOnly) {
    const auto path = test::path64({0, 0, 1000, 0, 2000, -1, 3000, -1});
    next::internal::offset_state state;
    next::internal::assign_normals(state.normals, path);
    state.delta = 7.0;
    state.group_delta = 1.0;
    next::internal::offset_group group{
        next::Paths64{path}, next::JoinType::Miter, next::EndType::Butt};
    const next::DeltaCallback64 delta_callback =
        [](const next::Path64&, const next::PathD&, std::size_t, std::size_t) { return 4.0; };

    next::internal::append_offset_join(
        state,
        group,
        path,
        path,
        1,
        0,
        next::internal::offset_join_options{next::JoinType::Miter, 0.0, 0U},
        next::delta_callback_ref{delta_callback});

    EXPECT_DOUBLE_EQ(state.delta, 7.0);
    EXPECT_DOUBLE_EQ(state.group_delta, 4.0);
}
