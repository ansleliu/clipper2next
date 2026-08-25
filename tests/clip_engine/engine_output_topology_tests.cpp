#include "clip/engine/private/engine_output_topology.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineOutputTopologyTests, AddOutputPointInsertsAtFrontSide) {
    next::internal::engine_output_owner output_owner;
    auto* owner = &output_owner.create_outrec();
    next::internal::active_edge_node front_edge;
    next::internal::active_edge_node back_edge;
    auto* front_point = &output_owner.create_outpt({0, 0}, *owner);
    auto* back_point = &output_owner.create_outpt({10, 0}, *owner);

    front_point->next = back_point;
    front_point->prev = back_point;
    back_point->next = front_point;
    back_point->prev = front_point;
    owner->pts = front_point;
    owner->front_edge = &front_edge;
    owner->back_edge = &back_edge;
    front_edge.outrec = owner;
    back_edge.outrec = owner;

    auto* inserted = next::internal::add_output_point(front_edge, {5, 0});

    EXPECT_EQ(inserted->pt, next::Point64(5, 0));
    EXPECT_EQ(owner->pts.get(), inserted);
    EXPECT_EQ(front_point->next.get(), inserted);
    EXPECT_EQ(inserted->prev.get(), front_point);
    EXPECT_EQ(inserted->next.get(), back_point);
    EXPECT_EQ(back_point->prev.get(), inserted);
}

TEST(Clipper2NextEngineOutputTopologyTests, JoinOutrecPathsMovesSecondPathIntoFirst) {
    next::internal::Vertex ordinary_vertex;
    next::internal::output_record_node first_owner;
    next::internal::output_record_node second_owner;
    next::internal::active_edge_node first_front_edge;
    next::internal::active_edge_node first_back_edge;
    next::internal::active_edge_node second_front_edge;
    next::internal::active_edge_node second_back_edge;
    auto first_front = next::internal::output_point_node{{0, 0}, first_owner};
    auto first_back = next::internal::output_point_node{{10, 0}, first_owner};
    auto second_front = next::internal::output_point_node{{20, 0}, second_owner};
    auto second_back = next::internal::output_point_node{{30, 0}, second_owner};

    first_front.next = &first_back;
    first_front.prev = &first_back;
    first_back.next = &first_front;
    first_back.prev = &first_front;
    second_front.next = &second_back;
    second_front.prev = &second_back;
    second_back.next = &second_front;
    second_back.prev = &second_front;

    first_owner.pts = &first_front;
    first_owner.front_edge = &first_front_edge;
    first_owner.back_edge = &first_back_edge;
    second_owner.pts = &second_front;
    second_owner.front_edge = &second_front_edge;
    second_owner.back_edge = &second_back_edge;
    first_front_edge.outrec = &first_owner;
    first_back_edge.outrec = &first_owner;
    second_front_edge.outrec = &second_owner;
    second_back_edge.outrec = &second_owner;
    first_front_edge.vertex_top = &ordinary_vertex;

    next::internal::join_outrec_paths(first_front_edge, second_back_edge);

    EXPECT_EQ(first_owner.pts.get(), &second_front);
    EXPECT_EQ(first_owner.front_edge.get(), &second_front_edge);
    EXPECT_EQ(second_front_edge.outrec.get(), &first_owner);
    EXPECT_EQ(second_owner.pts.get(), nullptr);
    EXPECT_EQ(second_owner.owner.get(), &first_owner);
    EXPECT_EQ(first_front_edge.outrec.get(), nullptr);
    EXPECT_EQ(second_back_edge.outrec.get(), nullptr);
    EXPECT_EQ(second_front.next.get(), &first_back);
    EXPECT_EQ(first_back.prev.get(), &second_front);
    EXPECT_EQ(first_front.next.get(), &second_back);
    EXPECT_EQ(second_back.prev.get(), &first_front);
}

TEST(Clipper2NextEngineOutputTopologyTests, StartOpenPathCreatesOpenOutputRecord) {
    next::internal::engine_output_owner output_owner;
    next::internal::active_edge_node edge;
    edge.wind_dx = 1;

    auto* point = next::internal::start_open_path(output_owner, edge, {7, 9});

    ASSERT_EQ(output_owner.records().size(), 1U);
    auto* output_record = output_owner.records()[0].get();
    EXPECT_TRUE(output_record->is_open);
    EXPECT_EQ(output_record->front_edge.get(), &edge);
    EXPECT_EQ(output_record->back_edge.get(), nullptr);
    EXPECT_EQ(edge.outrec.get(), output_record);
    EXPECT_EQ(output_record->pts.get(), point);
    EXPECT_EQ(point->pt, next::Point64(7, 9));
    EXPECT_EQ(point->outrec.get(), output_record);
}

TEST(Clipper2NextEngineOutputTopologyTests, OutputTopologyAddLocalMinPolygonCreatesClosedRecord) {
    next::internal::clipper_base_state state;
    next::internal::Vertex vertex{{0, 0}};
    next::internal::local_minimum_node minima{vertex, next::PathType::Subject, false};
    next::internal::active_edge_node left;
    next::internal::active_edge_node right;
    left.local_min = &minima;
    right.local_min = &minima;

    auto* point = next::internal::add_local_min_polygon(state, left, right, {4, 5}, true);

    ASSERT_EQ(state.output_owner_.records().size(), 1U);
    auto* output_record = state.output_owner_.records()[0].get();
    EXPECT_EQ(left.outrec.get(), output_record);
    EXPECT_EQ(right.outrec.get(), output_record);
    EXPECT_EQ(output_record->front_edge.get(), &left);
    EXPECT_EQ(output_record->back_edge.get(), &right);
    EXPECT_FALSE(output_record->is_open);
    EXPECT_EQ(output_record->pts.get(), point);
    EXPECT_EQ(point->pt, next::Point64(4, 5));
}

TEST(Clipper2NextEngineOutputTopologyTests,
     OutputTopologyAddLocalMaxPolygonReportsInvalidFrontFrontJoin) {
    next::internal::clipper_base_state state;
    next::internal::output_record_node first_record;
    next::internal::output_record_node second_record;
    next::internal::Vertex first_top{{0, 0}};
    next::internal::Vertex second_top{{10, 0}};
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    first_record.front_edge = &first;
    second_record.front_edge = &second;
    first.outrec = &first_record;
    second.outrec = &second_record;
    first.vertex_top = &first_top;
    second.vertex_top = &second_top;

    auto result = next::internal::add_local_max_polygon(
        state, first, second, {5, 5}, [](next::internal::active_edge_node&, const next::Point64&) {
        });

    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.output_point.has_value());
}

TEST(Clipper2NextEngineOutputTopologyTests, OutputTopologySplitJoinedEdgeClearsJoinAndCreatesLocalMin) {
    next::internal::clipper_base_state state;
    next::internal::Vertex vertex{{0, 0}};
    next::internal::local_minimum_node minima{vertex, next::PathType::Subject, false};
    next::internal::active_edge_node edge;
    next::internal::active_edge_node next_edge;
    edge.local_min = &minima;
    next_edge.local_min = &minima;
    edge.next_in_ael = &next_edge;
    next_edge.prev_in_ael = &edge;
    edge.join_with = next::JoinWith::Right;
    next_edge.join_with = next::JoinWith::Left;

    next::internal::split_joined_edge(state, edge, {2, 3});

    EXPECT_EQ(edge.join_with, next::JoinWith::NoJoin);
    EXPECT_EQ(next_edge.join_with, next::JoinWith::NoJoin);
    ASSERT_EQ(state.output_owner_.records().size(), 1U);
    EXPECT_EQ(edge.outrec.get(), state.output_owner_.records()[0].get());
    EXPECT_EQ(next_edge.outrec.get(), state.output_owner_.records()[0].get());
}

TEST(Clipper2NextEngineOutputTopologyTests, OutputTopologyCheckJoinLeftNoOpsForOpenEdges) {
    next::internal::clipper_base_state state;
    next::internal::Vertex vertex{{0, 0}};
    next::internal::local_minimum_node open_minima{vertex, next::PathType::Subject, true};
    next::internal::active_edge_node previous;
    next::internal::active_edge_node edge;
    previous.local_min = &open_minima;
    edge.local_min = &open_minima;
    previous.outrec = &state.output_owner_.create_outrec();
    edge.outrec = &state.output_owner_.create_outrec();
    previous.next_in_ael = &edge;
    edge.prev_in_ael = &previous;
    bool succeeded = true;

    next::internal::check_join_left(state, succeeded, edge, {0, 0}, false);

    EXPECT_TRUE(succeeded);
    EXPECT_EQ(previous.join_with, next::JoinWith::NoJoin);
    EXPECT_EQ(edge.join_with, next::JoinWith::NoJoin);
}
