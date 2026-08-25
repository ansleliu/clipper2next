#include "clipper2next/api/memory.h"
#include "offset/private/offset_state.h"
#include "offset/private/offset_thread_state.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextOffsetStateTests, ResetClearsPerExecutionBuffers) {
    next::internal::offset_state state;
    state.delta = 10.0;
    state.group_delta = -4.0;
    state.temp_limit = 2.0;
    state.arc = next::internal::offset_arc_parameters{3.0, 0.5, 0.75};
    state.normals.push_back({1.0, 0.0});
    state.path_out.push_back({1, 2});

    state.reset();

    EXPECT_EQ(state.delta, 0.0);
    EXPECT_EQ(state.group_delta, 0.0);
    EXPECT_EQ(state.temp_limit, 0.0);
    EXPECT_EQ(state.arc.steps_per_rad, 0.0);
    EXPECT_EQ(state.arc.step_sin, 0.0);
    EXPECT_EQ(state.arc.step_cos, 0.0);
    EXPECT_TRUE(state.normals.empty());
    EXPECT_TRUE(state.path_out.empty());
}

TEST(Clipper2NextOffsetStateTests, ReleaseThreadCachesFreesReusableOffsetBuffers) {
    auto& state = next::internal::acquire_reusable_offset_state();
    state.normals.reserve(128);
    state.path_out.reserve(128);

    next::release_thread_caches();

    auto& released_state = next::internal::acquire_reusable_offset_state();
    EXPECT_EQ(released_state.normals.capacity(), 0U);
    EXPECT_EQ(released_state.path_out.capacity(), 0U);
}
