#include "clip/engine/private/engine_fill_rule.h"
#include "clip/engine/private/engine_winding.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineWindingTests, WindingContributionDelegatesToFillRule) {
    next::internal::Vertex vertex{{0, 0}};
    next::internal::local_minimum_node minima{vertex, next::PathType::Subject, false};
    next::internal::active_edge_node edge;
    edge.local_min = &minima;
    edge.winding_count = 1;
    edge.wind_cnt2 = 0;

    EXPECT_EQ(next::internal::is_contributing_closed_edge(
                  next::ClipType::Union, next::FillRule::NonZero, edge),
              next::internal::is_contributing_closed(next::ClipType::Union,
                                                     next::FillRule::NonZero,
                                                     next::PathType::Subject,
                                                     edge.winding_count,
                                                     edge.wind_cnt2));
}

TEST(Clipper2NextEngineWindingTests, WindingClosedFirstEdgeUsesOwnWindDirection) {
    next::internal::clipper_base_state state;
    next::internal::Vertex vertex{{0, 0}};
    next::internal::local_minimum_node minima{vertex, next::PathType::Subject, false};
    next::internal::active_edge_node edge;
    edge.local_min = &minima;
    edge.wind_dx = -1;
    state.actives_ = &edge;
    state.fillrule_ = next::FillRule::NonZero;

    next::internal::set_wind_count_for_closed_path_edge(state, edge);

    EXPECT_EQ(edge.winding_count, -1);
    EXPECT_EQ(edge.wind_cnt2, 0);
}

TEST(Clipper2NextEngineWindingTests, WindingClosedEvenOddCopiesPreviousOppositeCount) {
    next::internal::clipper_base_state state;
    next::internal::Vertex vertex{{0, 0}};
    next::internal::local_minimum_node minima{vertex, next::PathType::Subject, false};
    next::internal::active_edge_node previous;
    next::internal::active_edge_node edge;
    previous.local_min = &minima;
    previous.wind_cnt2 = 1;
    previous.next_in_ael = &edge;
    edge.local_min = &minima;
    edge.prev_in_ael = &previous;
    edge.wind_dx = 1;
    state.actives_ = &previous;
    state.fillrule_ = next::FillRule::EvenOdd;

    next::internal::set_wind_count_for_closed_path_edge(state, edge);

    EXPECT_EQ(edge.winding_count, 1);
    EXPECT_EQ(edge.wind_cnt2, 1);
}

TEST(Clipper2NextEngineWindingTests, WindingClosedNonZeroAccumulatesOppositePolytype) {
    next::internal::clipper_base_state state;
    next::internal::Vertex subject_vertex{{0, 0}};
    next::internal::Vertex clip_vertex{{0, 0}};
    next::internal::local_minimum_node subject_minima{
        subject_vertex, next::PathType::Subject, false};
    next::internal::local_minimum_node clip_minima{clip_vertex, next::PathType::Clip, false};
    next::internal::active_edge_node clip_edge;
    next::internal::active_edge_node subject_edge;
    clip_edge.local_min = &clip_minima;
    clip_edge.wind_dx = 1;
    clip_edge.next_in_ael = &subject_edge;
    subject_edge.local_min = &subject_minima;
    subject_edge.prev_in_ael = &clip_edge;
    subject_edge.wind_dx = 1;
    state.actives_ = &clip_edge;
    state.fillrule_ = next::FillRule::NonZero;

    next::internal::set_wind_count_for_closed_path_edge(state, subject_edge);

    EXPECT_EQ(subject_edge.winding_count, 1);
    EXPECT_EQ(subject_edge.wind_cnt2, 1);
}

TEST(Clipper2NextEngineWindingTests, WindingOpenEvenOddCountsPriorClipAndClosedSubject) {
    next::internal::clipper_base_state state;
    next::internal::Vertex subject_vertex{{0, 0}};
    next::internal::Vertex clip_vertex{{0, 0}};
    next::internal::Vertex open_vertex{{0, 0}};
    next::internal::local_minimum_node subject_minima{
        subject_vertex, next::PathType::Subject, false};
    next::internal::local_minimum_node clip_minima{clip_vertex, next::PathType::Clip, false};
    next::internal::local_minimum_node open_minima{open_vertex, next::PathType::Subject, true};
    next::internal::active_edge_node subject_edge;
    next::internal::active_edge_node clip_edge;
    next::internal::active_edge_node open_edge;
    subject_edge.local_min = &subject_minima;
    subject_edge.next_in_ael = &clip_edge;
    clip_edge.local_min = &clip_minima;
    clip_edge.prev_in_ael = &subject_edge;
    clip_edge.next_in_ael = &open_edge;
    open_edge.local_min = &open_minima;
    open_edge.prev_in_ael = &clip_edge;
    state.actives_ = &subject_edge;
    state.fillrule_ = next::FillRule::EvenOdd;

    next::internal::set_wind_count_for_open_path_edge(state, open_edge);

    EXPECT_EQ(open_edge.winding_count, 1);
    EXPECT_EQ(open_edge.wind_cnt2, 1);
}

TEST(Clipper2NextEngineWindingTests, WindingOpenNonZeroAccumulatesSignedPriorCounts) {
    next::internal::clipper_base_state state;
    next::internal::Vertex subject_vertex{{0, 0}};
    next::internal::Vertex clip_vertex{{0, 0}};
    next::internal::Vertex open_vertex{{0, 0}};
    next::internal::local_minimum_node subject_minima{
        subject_vertex, next::PathType::Subject, false};
    next::internal::local_minimum_node clip_minima{clip_vertex, next::PathType::Clip, false};
    next::internal::local_minimum_node open_minima{open_vertex, next::PathType::Subject, true};
    next::internal::active_edge_node subject_edge;
    next::internal::active_edge_node clip_edge;
    next::internal::active_edge_node open_edge;
    subject_edge.local_min = &subject_minima;
    subject_edge.wind_dx = -1;
    subject_edge.next_in_ael = &clip_edge;
    clip_edge.local_min = &clip_minima;
    clip_edge.wind_dx = 1;
    clip_edge.prev_in_ael = &subject_edge;
    clip_edge.next_in_ael = &open_edge;
    open_edge.local_min = &open_minima;
    open_edge.prev_in_ael = &clip_edge;
    state.actives_ = &subject_edge;
    state.fillrule_ = next::FillRule::NonZero;

    next::internal::set_wind_count_for_open_path_edge(state, open_edge);

    EXPECT_EQ(open_edge.winding_count, -1);
    EXPECT_EQ(open_edge.wind_cnt2, 1);
}
