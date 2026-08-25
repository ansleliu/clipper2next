#include "clip/engine/private/engine_ael_builder.h"
#include "clip/engine/private/engine_geometry.h"
#include "clip/engine/private/engine_lifecycle.h"
#include "clip/engine/private/engine_scanline.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineAelBuilderTests, AelBuilderInsertLeftEdgeMaintainsCurrentXOrder) {
    next::internal::clipper_base_state state;
    next::internal::active_edge_node resident;
    next::internal::active_edge_node newcomer;
    resident.current_x = 10;
    newcomer.current_x = 5;
    state.actives_ = &resident;

    next::internal::insert_left_edge(state, newcomer);

    EXPECT_EQ(state.actives_, &newcomer);
    EXPECT_EQ(newcomer.next_in_ael.get(), &resident);
    EXPECT_EQ(resident.prev_in_ael.get(), &newcomer);
}

TEST(Clipper2NextEngineAelBuilderTests, AelBuilderAdjustCurrXAndCopyToSelUsesAelLinks) {
    next::internal::clipper_base_state state;
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    first.bottom = {0, 10};
    first.top_point = {10, 0};
    first.dx = next::internal::get_dx(first.bottom, first.top_point);
    first.next_in_ael = &second;
    second.bottom = {10, 10};
    second.top_point = {20, 0};
    second.dx = next::internal::get_dx(second.bottom, second.top_point);
    second.prev_in_ael = &first;
    state.actives_ = &first;

    next::internal::adjust_curr_x_and_copy_to_sel(state, 5);

    EXPECT_EQ(state.sel_, &first);
    EXPECT_EQ(first.next_in_sel.get(), &second);
    EXPECT_EQ(second.prev_in_sel.get(), &first);
    EXPECT_EQ(first.jump.get(), &second);
    EXPECT_EQ(second.jump.get(), nullptr);
    EXPECT_EQ(first.current_x, next::internal::top_x(first, 5));
    EXPECT_EQ(second.current_x, next::internal::top_x(second, 5));
}

TEST(Clipper2NextEngineAelBuilderTests, AelBuilderUpdateEdgePromotesAndQueuesNonHorizontal) {
    next::internal::clipper_base_state state;
    next::internal::Vertex mid{{5, 5}};
    next::internal::Vertex top_point{{10, 0}};
    mid.next = &top_point;
    top_point.prev = &mid;
    next::internal::active_edge_node edge;
    edge.bottom = {0, 10};
    edge.top_point = mid.pt;
    edge.vertex_top = &mid;
    edge.wind_dx = 1;
    int split_count = 0;
    int check_left_count = 0;
    int check_right_count = 0;

    next::internal::update_edge_into_ael(
        state,
        edge,
        true,
        [&](next::internal::active_edge_node&, const next::Point64&) { ++split_count; },
        [&](next::internal::active_edge_node&, const next::Point64&) { ++check_left_count; },
        [&](next::internal::active_edge_node&, const next::Point64&, bool check_curr_x) {
            EXPECT_TRUE(check_curr_x);
            ++check_right_count;
        });

    EXPECT_EQ(edge.bottom, mid.pt);
    EXPECT_EQ(edge.vertex_top.get(), &top_point);
    EXPECT_EQ(edge.top_point, top_point.pt);
    EXPECT_EQ(edge.current_x, mid.pt.x);
    EXPECT_EQ(split_count, 0);
    EXPECT_EQ(check_left_count, 1);
    EXPECT_EQ(check_right_count, 1);

    int64_t scanline = 0;
    ASSERT_TRUE(next::internal::pop_scanline(state.scanline_list_, scanline));
    EXPECT_EQ(scanline, top_point.pt.y);
}

TEST(Clipper2NextEngineAelBuilderTests, AelBuilderInsertLocalMinimaStartsContributingOpenPath) {
    next::internal::clipper_base_state state;
    state.cliptype_ = next::ClipType::Union;
    state.fillrule_ = next::FillRule::EvenOdd;
    next::Paths64 paths{{{0, 10}, {10, 0}, {20, 0}}};
    next::internal::add_paths_to_state(state, paths, next::PathType::Subject, true);
    next::internal::reset_engine_state(state);
    int64_t bot_y = 0;
    ASSERT_TRUE(next::internal::pop_scanline(state.scanline_list_, bot_y));
    ASSERT_EQ(bot_y, 10);
    int local_min_count = 0;
    int check_left_count = 0;
    int check_right_count = 0;
    int intersect_count = 0;

    next::internal::insert_local_minima_into_ael(
        state,
        bot_y,
        [&](next::internal::active_edge_node&,
            next::internal::active_edge_node&,
            const next::Point64&,
            bool) {
            ++local_min_count;
            return static_cast<next::internal::output_point_node*>(nullptr);
        },
        [&](next::internal::active_edge_node&, const next::Point64&, bool) { ++check_left_count; },
        [&](next::internal::active_edge_node&, const next::Point64&, bool) { ++check_right_count; },
        [&](next::internal::active_edge_node&,
            next::internal::active_edge_node&,
            const next::Point64&) { ++intersect_count; });

    ASSERT_NE(state.actives_, nullptr);
    EXPECT_TRUE(next::internal::is_open(*state.actives_));
    EXPECT_EQ(state.output_owner_.records().size(), 1U);
    EXPECT_EQ(state.output_owner_.records()[0].get()->front_edge.get(), state.actives_);
    EXPECT_EQ(local_min_count, 0);
    EXPECT_EQ(check_left_count, 0);
    EXPECT_EQ(check_right_count, 0);
    EXPECT_EQ(intersect_count, 0);

    int64_t scanline = 0;
    ASSERT_TRUE(next::internal::pop_scanline(state.scanline_list_, scanline));
    EXPECT_EQ(scanline, 0);

    next::internal::cleanup_engine_state(state);
    next::internal::dispose_vertices_and_local_minima(state);
}
