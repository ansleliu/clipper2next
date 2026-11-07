// Observe the actual retained engine through its private lease owner.
#include "clip/private/owning_clip_state.h"
#include "clip/private/clip_execution_strategy.h"
#include "clipper2next/clip.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextOwningClipEmptyPhaseTests, EmptyOperationsLeaveRetainedEngineUntouched) {
    for (const auto operation : {next::ClipType::NoClip, next::ClipType::Union}) {
        auto request = next::clip_request64{};
        request.clip_type = operation;
        if (operation == next::ClipType::NoClip) {
            request.subjects = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
        } else {
            request.subjects.emplace_back();
            request.open_subjects.emplace_back();
            request.clips.emplace_back();
        }
        const auto prepared = next::prepare_clip_request(request);
        next::internal::clipper_base_state* cached_state;
        {
            auto lease = next::internal::owning_clip_state_lease{};
            cached_state = &lease.state();
        }
        auto& state = *cached_state;
        const auto mark = [&] {
            state.cliptype_ = next::ClipType::Difference;
            state.minima_list_sorted_ = true;
        };
        const auto expect_untouched = [&] {
            EXPECT_EQ(state.cliptype_, next::ClipType::Difference);
            EXPECT_TRUE(state.minima_list_sorted_);
            EXPECT_EQ(state.vertex_lists_.size(), 0U);
        };

        mark();
        const auto flat = next::clip_checked(request);
        ASSERT_TRUE(flat);
        EXPECT_TRUE(flat->closed.empty());
        EXPECT_TRUE(flat->open.empty());
        expect_untouched();

        mark();
        const auto cached = next::clip_checked(prepared);
        ASSERT_TRUE(cached);
        EXPECT_TRUE(cached->closed.empty());
        EXPECT_TRUE(cached->open.empty());
        expect_untouched();

        mark();
        const auto tree = next::clip_tree_checked(request);
        ASSERT_TRUE(tree);
        EXPECT_EQ(tree->tree.count(), 0U);
        EXPECT_TRUE(tree->open.empty());
        expect_untouched();

        mark();
        EXPECT_TRUE(next::clip(request).closed.empty());
        expect_untouched();

        mark();
        EXPECT_TRUE(next::clip(prepared).closed.empty());
        expect_untouched();

        mark();
        EXPECT_EQ(next::clip_tree(request).tree.count(), 0U);
        expect_untouched();
    }
    next::internal::release_clip_thread_state();
}
