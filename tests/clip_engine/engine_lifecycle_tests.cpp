#include "clip/engine/private/engine_lifecycle.h"
#include "clip/engine/private/engine_scanline.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineLifecycleTests, LifecycleAddLocalMinimumMarksVertexAndSkipsDuplicate) {
    next::internal::Vertex vertex{{3, 4}};
    next::internal::LocalMinimaList minima;

    next::internal::add_local_minimum(minima, vertex, next::PathType::Subject, true);

    ASSERT_EQ(minima.size(), 1U);
    EXPECT_EQ(&minima[0].vertex.get(), &vertex);
    EXPECT_EQ(minima[0].polytype, next::PathType::Subject);
    EXPECT_TRUE(minima[0].is_open);
    EXPECT_NE(vertex.flags & next::internal::VertexFlags::LocalMin,
              next::internal::VertexFlags::Empty);

    next::internal::add_local_minimum(minima, vertex, next::PathType::Clip, false);

    ASSERT_EQ(minima.size(), 1U);
    EXPECT_EQ(minima[0].polytype, next::PathType::Subject);
    EXPECT_TRUE(minima[0].is_open);
}

TEST(Clipper2NextEngineLifecycleTests, LifecycleAddPathsBuildsOpenPathVerticesAndMinima) {
    next::internal::reuseable_data_state state;
    next::Paths64 paths{{{0, 10}, {10, 0}, {20, 0}}};

    next::internal::add_paths_to_state(state, paths, next::PathType::Subject, true);

    ASSERT_EQ(state.vertex_lists_.size(), 1U);
    ASSERT_EQ(state.minima_list_.size(), 1U);
    EXPECT_EQ(state.minima_list_[0].vertex.get().pt, next::Point64(0, 10));
    EXPECT_EQ(state.minima_list_[0].polytype, next::PathType::Subject);
    EXPECT_TRUE(state.minima_list_[0].is_open);
    EXPECT_NE(state.minima_list_[0].vertex.get().flags & next::internal::VertexFlags::OpenStart,
              next::internal::VertexFlags::Empty);

    next::internal::dispose_vertices_and_local_minima(state);
}

TEST(Clipper2NextEngineLifecycleTests, LifecycleDisposeVerticesClearsReusableDataState) {
    next::internal::reuseable_data_state state;
    next::internal::Vertex vertex{{1, 2}};
    state.minima_list_.emplace_back(vertex, next::PathType::Clip, false);
    static_cast<void>(state.vertex_lists_.acquire(2));

    next::internal::dispose_vertices_and_local_minima(state);

    EXPECT_TRUE(state.minima_list_.empty());
    EXPECT_EQ(state.vertex_lists_.size(), 0U);
}

TEST(Clipper2NextEngineLifecycleTests, LifecycleDisposeActiveEdgesNullsHead) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    first.next_in_ael = &second;
    second.prev_in_ael = &first;
    next::internal::active_edge_node* head = &first;

    next::internal::dispose_active_edges(head);

    EXPECT_EQ(head, nullptr);
    EXPECT_EQ(first.next_in_ael.get(), nullptr);
    EXPECT_EQ(second.prev_in_ael.get(), nullptr);
}

TEST(Clipper2NextEngineLifecycleTests, LifecycleCleanupEngineStateClearsTransientOutput) {
    next::internal::clipper_base_state state;
    auto* first = &state.active_pool_.emplace();
    auto* second = &state.active_pool_.emplace();
    first->next_in_ael = second;
    state.actives_ = first;
    next::Point64 intersection{1, 2};
    state.intersect_nodes_.emplace_back(*first, *second, intersection);
    next::internal::push_scanline(state.scanline_list_, 30);
    state.horz_seg_list_.emplace_back();
    state.horz_join_list_.emplace_back();
    static_cast<void>(state.output_owner_.create_outrec());

    next::internal::cleanup_engine_state(state);

    EXPECT_EQ(state.actives_, nullptr);
    EXPECT_TRUE(state.scanline_list_.empty());
    EXPECT_TRUE(state.intersect_nodes_.empty());
    EXPECT_TRUE(state.horz_seg_list_.empty());
    EXPECT_TRUE(state.horz_join_list_.empty());
    EXPECT_TRUE(state.active_pool_.empty());
    EXPECT_TRUE(state.output_owner_.records().empty());
}

TEST(Clipper2NextEngineLifecycleTests, LifecycleResetEngineStateSortsMinimaAndQueuesScanlines) {
    next::internal::clipper_base_state state;
    next::internal::Vertex low{{0, 10}};
    next::internal::Vertex high{{0, 30}};
    next::internal::active_edge_node active;
    state.minima_list_.emplace_back(low, next::PathType::Subject, false);
    state.minima_list_.emplace_back(high, next::PathType::Subject, false);
    state.actives_ = &active;
    state.sel_ = &active;
    state.minima_list_sorted_ = false;

    next::internal::reset_engine_state(state);

    ASSERT_EQ(state.minima_list_.size(), 2U);
    EXPECT_EQ(&state.minima_list_[0].vertex.get(), &high);
    EXPECT_EQ(&state.minima_list_[1].vertex.get(), &low);
    EXPECT_EQ(state.current_locmin_iter_, state.minima_list_.begin());
    EXPECT_EQ(state.actives_, nullptr);
    EXPECT_EQ(state.sel_, nullptr);
    EXPECT_TRUE(state.minima_list_sorted_);

    int64_t scanline = 0;
    ASSERT_TRUE(next::internal::pop_scanline(state.scanline_list_, scanline));
    EXPECT_EQ(scanline, 30);
    ASSERT_TRUE(next::internal::pop_scanline(state.scanline_list_, scanline));
    EXPECT_EQ(scanline, 10);
}
