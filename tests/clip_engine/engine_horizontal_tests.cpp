#include "clip/engine/private/engine_horizontal.h"
#include "clip/engine/private/engine_output_owner.h"

#include <gtest/gtest.h>

#include <vector>

namespace next = clipper2next;

TEST(Clipper2NextEngineHorizontalTests, HorizontalSegmentsSortValidSegmentsByLeftX) {
    next::internal::output_record_node owner;
    auto left_a = next::internal::output_point_node{{20, 0}, owner};
    auto right_a = next::internal::output_point_node{{30, 0}, owner};
    auto left_b = next::internal::output_point_node{{5, 0}, owner};
    auto right_b = next::internal::output_point_node{{10, 0}, owner};

    next::internal::horizontal_segment_node first{left_a};
    first.set_right_point(right_a);
    next::internal::horizontal_segment_node second{left_b};
    second.set_right_point(right_b);
    next::internal::HorzSegmentList segments{
        first, second, next::internal::horizontal_segment_node{}};

    next::internal::sort_horizontal_segments(segments);

    EXPECT_EQ(&segments[0].left_point(), &left_b);
    EXPECT_EQ(&segments[1].left_point(), &left_a);
    EXPECT_FALSE(segments[2].has_right_point());
}

TEST(Clipper2NextEngineHorizontalTests, HorizontalSegmentHeadingChoosesLeftToRight) {
    next::internal::output_record_node owner;
    auto left = next::internal::output_point_node{{5, 0}, owner};
    auto right = next::internal::output_point_node{{20, 0}, owner};
    next::internal::horizontal_segment_node segment{right};

    EXPECT_TRUE(next::internal::set_horizontal_segment_heading_forward(segment, left, right));
    EXPECT_EQ(&segment.left_point(), &left);
    EXPECT_EQ(&segment.right_point(), &right);
    EXPECT_TRUE(segment.left_to_right);

    EXPECT_TRUE(next::internal::set_horizontal_segment_heading_forward(segment, right, left));
    EXPECT_EQ(&segment.left_point(), &left);
    EXPECT_EQ(&segment.right_point(), &right);
    EXPECT_FALSE(segment.left_to_right);
}

TEST(Clipper2NextEngineHorizontalTests, ResetHorizontalDirectionClassifiesHorizontalTravel) {
    next::internal::active_edge_node edge;
    next::internal::Vertex max_vertex;
    int64_t horizontal_left = 0;
    int64_t horizontal_right = 0;

    edge.bottom = {0, 0};
    edge.top_point = {20, 0};
    edge.current_x = 5;

    EXPECT_TRUE(next::internal::reset_horizontal_direction(
        edge, &max_vertex, horizontal_left, horizontal_right));
    EXPECT_EQ(horizontal_left, 5);
    EXPECT_EQ(horizontal_right, 20);

    edge.top_point = {-10, 0};

    EXPECT_FALSE(next::internal::reset_horizontal_direction(
        edge, &max_vertex, horizontal_left, horizontal_right));
    EXPECT_EQ(horizontal_left, -10);
    EXPECT_EQ(horizontal_right, 5);
}

TEST(Clipper2NextEngineHorizontalTests, ResetHorizontalDirectionFindsVerticalMaxima) {
    next::internal::active_edge_node horizontal;
    next::internal::active_edge_node follower;
    next::internal::Vertex max_vertex;
    next::internal::Vertex other_vertex;
    int64_t horizontal_left = 0;
    int64_t horizontal_right = 0;

    horizontal.bottom = {10, 0};
    horizontal.top_point = {10, 0};
    horizontal.current_x = 10;
    horizontal.next_in_ael = &follower;
    follower.vertex_top = &max_vertex;

    EXPECT_TRUE(next::internal::reset_horizontal_direction(
        horizontal, &max_vertex, horizontal_left, horizontal_right));
    EXPECT_EQ(horizontal_left, 10);
    EXPECT_EQ(horizontal_right, 10);

    follower.vertex_top = &other_vertex;
    EXPECT_FALSE(next::internal::reset_horizontal_direction(
        horizontal, &max_vertex, horizontal_left, horizontal_right));
}

TEST(Clipper2NextEngineHorizontalTests, AddTrialHorizontalJoinKeepsOnlyClosedOutput) {
    next::internal::output_record_node closed_owner;
    next::internal::output_record_node open_owner;
    open_owner.is_open = true;
    auto closed_point = next::internal::output_point_node{{5, 0}, closed_owner};
    auto open_point = next::internal::output_point_node{{10, 0}, open_owner};
    next::internal::HorzSegmentList segments;

    next::internal::add_trial_horizontal_join(segments, &closed_point);

    ASSERT_EQ(segments.size(), 1U);
    EXPECT_EQ(&segments[0].left_point(), &closed_point);

    next::internal::add_trial_horizontal_join(segments, &open_point);

    ASSERT_EQ(segments.size(), 1U);
    EXPECT_EQ(&segments[0].left_point(), &closed_point);
}

TEST(Clipper2NextEngineHorizontalTests, ConvertHorizontalSegmentsCreatesOpposingOverlapJoin) {
    next::internal::engine_output_owner output_owner;
    auto* first_owner = &output_owner.create_outrec();
    auto* second_owner = &output_owner.create_outrec();
    auto* first_left = &output_owner.create_outpt({0, 0}, *first_owner);
    auto* first_right = &output_owner.create_outpt({100, 0}, *first_owner);
    auto* second_left = &output_owner.create_outpt({50, 0}, *second_owner);
    auto* second_right = &output_owner.create_outpt({150, 0}, *second_owner);

    first_left->next = first_right;
    first_left->prev = first_right;
    first_right->next = first_left;
    first_right->prev = first_left;
    first_owner->pts = first_left;

    second_left->next = second_right;
    second_left->prev = second_right;
    second_right->next = second_left;
    second_right->prev = second_left;
    second_owner->pts = second_left;

    next::internal::HorzSegmentList segments{next::internal::horizontal_segment_node{*first_right},
                                             next::internal::horizontal_segment_node{*second_left}};
    std::vector<next::internal::horizontal_join_node> joins;

    next::internal::convert_horizontal_segments_to_joins(segments, joins);

    ASSERT_EQ(joins.size(), 1U);
    EXPECT_EQ(joins[0].first_point().outrec.get(), first_owner);
    EXPECT_EQ(joins[0].second_point().outrec.get(), second_owner);
    EXPECT_NE(&joins[0].first_point(), first_left);
    EXPECT_NE(&joins[0].second_point(), second_left);
}

TEST(Clipper2NextEngineHorizontalTests, ProcessHorizontalJoinsLinksDistinctOutputRecords) {
    next::internal::output_record_node first_owner;
    next::internal::output_record_node second_owner;
    auto first_a = next::internal::output_point_node{{0, 0}, first_owner};
    auto first_b = next::internal::output_point_node{{10, 0}, first_owner};
    auto second_a = next::internal::output_point_node{{5, 0}, second_owner};
    auto second_b = next::internal::output_point_node{{15, 0}, second_owner};

    first_a.next = &first_b;
    first_a.prev = &first_b;
    first_b.next = &first_a;
    first_b.prev = &first_a;
    first_owner.pts = &first_a;

    second_a.next = &second_b;
    second_a.prev = &second_b;
    second_b.next = &second_a;
    second_b.prev = &second_a;
    second_owner.pts = &second_a;

    std::vector<next::internal::horizontal_join_node> joins{
        next::internal::horizontal_join_node{first_a, second_a}};
    next::internal::engine_output_owner output_owner;

    next::internal::process_horizontal_joins(joins, output_owner, false);

    EXPECT_EQ(first_a.next.get(), &second_a);
    EXPECT_EQ(second_a.prev.get(), &first_a);
    EXPECT_EQ(second_b.next.get(), &first_b);
    EXPECT_EQ(first_b.prev.get(), &second_b);
    EXPECT_EQ(second_owner.pts.get(), nullptr);
    EXPECT_EQ(second_owner.owner.get(), &first_owner);
}

TEST(Clipper2NextEngineHorizontalTests, HorizontalSegmentUpdateIgnoresInvalidSinglePointSegment) {
    next::internal::output_record_node owner;
    auto point = next::internal::output_point_node{{5, 0}, owner};
    owner.pts = &point;
    next::internal::horizontal_segment_node segment{point};

    EXPECT_FALSE(next::internal::update_horizontal_segment(segment));
    EXPECT_FALSE(segment.has_right_point());
}

TEST(Clipper2NextEngineHorizontalTests, HorizontalStackPushPopUsesSelLinks) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    next::internal::active_edge_node* stack = nullptr;

    next::internal::push_horizontal(stack, first);
    next::internal::push_horizontal(stack, second);

    next::internal::active_edge_node* popped = nullptr;
    ASSERT_TRUE(next::internal::pop_horizontal(stack, popped));
    EXPECT_EQ(popped, &second);
    EXPECT_EQ(stack, &first);

    ASSERT_TRUE(next::internal::pop_horizontal(stack, popped));
    EXPECT_EQ(popped, &first);
    EXPECT_EQ(stack, nullptr);

    EXPECT_FALSE(next::internal::pop_horizontal(stack, popped));
}
