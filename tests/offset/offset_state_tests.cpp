#include "clipper2next/api/memory.h"
#include "offset/private/offset_state.h"
#include "offset/private/offset_thread_state.h"

#include <gtest/gtest.h>
#include <stdexcept>

namespace next = clipper2next;

TEST(Clipper2NextOffsetStateTests, ResetClearsPerExecutionBuffers) {
    next::internal::offset_state state;
    state.group_delta = -4.0;
    state.temp_limit = 2.0;
    state.arc = next::internal::offset_arc_parameters{3.0, 0.5, 0.75};
    state.normals.push_back({1.0, 0.0});
    state.path_out.push_back({1, 2});

    state.reset();

    EXPECT_EQ(state.group_delta, 0.0);
    EXPECT_EQ(state.temp_limit, 0.0);
    EXPECT_EQ(state.arc.steps_per_rad, 0.0);
    EXPECT_EQ(state.arc.step_sin, 0.0);
    EXPECT_EQ(state.arc.step_cos, 0.0);
    EXPECT_TRUE(state.normals.empty());
    EXPECT_TRUE(state.path_out.empty());
}

TEST(Clipper2NextOffsetStateTests, ReleaseThreadCachesFreesReusableOffsetBuffers) {
    {
        auto lease = next::internal::offset_state_lease{};
        lease.state().normals.reserve(128);
        lease.state().path_out.reserve(128);
        next::release_thread_caches();
        EXPECT_GE(lease.state().normals.capacity(), 128U);
    }
    auto lease = next::internal::offset_state_lease{};
    EXPECT_EQ(lease.state().normals.capacity(), 0U);
    EXPECT_EQ(lease.state().path_out.capacity(), 0U);
}

TEST(Clipper2NextOffsetStateTests, LeaseExitRestoresReusableStateBeforeNextAcquisition) {
    next::internal::offset_state* cached = nullptr;
    const auto dirty = [](next::internal::offset_state& state) {
        state.group_delta = -7.0;
        state.temp_limit = 3.0;
        state.arc = {5.0, 0.25, 0.75};
        state.normals.push_back({1.0, 0.0});
        state.path_out.push_back({2, 3});
        state.callback_path.push_back({4, 5});
    };
    const auto expect_ready = [&] {
        ASSERT_NE(cached, nullptr);
        EXPECT_EQ(cached->group_delta, 0.0);
        EXPECT_EQ(cached->temp_limit, 0.0);
        EXPECT_EQ(cached->arc.steps_per_rad, 0.0);
        EXPECT_EQ(cached->arc.step_sin, 0.0);
        EXPECT_EQ(cached->arc.step_cos, 0.0);
        EXPECT_TRUE(cached->normals.empty());
        EXPECT_TRUE(cached->path_out.empty());
        EXPECT_TRUE(cached->callback_path.empty());
        EXPECT_EQ(cached->normals_charge.bytes(), 0U);
        EXPECT_EQ(cached->path_out_charge.bytes(), 0U);
        EXPECT_EQ(cached->callback_charge.bytes(), 0U);
    };
    {
        next::internal::offset_state_lease lease;
        cached = &lease.state();
        dirty(*cached);
    }
    expect_ready(); // Observe the live cached object before acquiring it again.
    try {
        next::internal::offset_state_lease lease;
        ASSERT_EQ(&lease.state(), cached);
        dirty(lease.state());
        throw std::runtime_error("expected lease unwind");
    } catch (const std::runtime_error&) {}
    expect_ready();
}
