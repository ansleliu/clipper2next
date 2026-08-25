#include "clip/engine/private/engine_geometry.h"
#include "clip/engine/private/engine_intersections.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace next = clipper2next;

static_assert(sizeof(next::internal::IntersectNode) <=
                  sizeof(next::Point64) + 2U * sizeof(void*) + 8U,
              "IntersectNode is a hot scanbeam value and must stay close to "
              "point-plus-two-edge-reference size");

TEST(Clipper2NextEngineIntersectionsTests, IntersectionsSortBottomUpThenLeftToRight) {
    std::vector<next::internal::IntersectNode> nodes;
    next::Point64 top_right{20, 10};
    next::Point64 bottom_left{5, 20};
    next::Point64 bottom_right{10, 20};
    nodes.emplace_back(next::internal::IntersectNode::from_point(top_right));
    nodes.emplace_back(next::internal::IntersectNode::from_point(bottom_right));
    nodes.emplace_back(next::internal::IntersectNode::from_point(bottom_left));

    next::internal::sort_intersections(nodes);

    ASSERT_EQ(nodes.size(), 3U);
    EXPECT_EQ(nodes[0].pt, bottom_left);
    EXPECT_EQ(nodes[1].pt, bottom_right);
    EXPECT_EQ(nodes[2].pt, top_right);
}

TEST(Clipper2NextEngineIntersectionsTests, IntersectionsSortKeepsEqualPointOrder) {
    next::internal::active_edge_node first_edge;
    next::internal::active_edge_node second_edge;
    next::internal::active_edge_node partner;
    next::Point64 point{10, 20};
    std::vector<next::internal::IntersectNode> nodes;
    nodes.emplace_back(first_edge, partner, point);
    nodes.emplace_back(second_edge, partner, point);

    next::internal::sort_intersections(nodes);

    ASSERT_EQ(nodes.size(), 2U);
    EXPECT_EQ(&nodes[0].first_edge(), &first_edge);
    EXPECT_EQ(&nodes[1].first_edge(), &second_edge);
}

TEST(Clipper2NextEngineIntersectionsTests, FindNextAdjacentIntersectionReturnsEndWhenInvariantBreaks) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    next::internal::active_edge_node third;
    next::internal::active_edge_node fourth;
    next::internal::IntersectNodeList nodes;
    nodes.emplace_back(first, second, next::Point64{0, 0});
    nodes.emplace_back(third, fourth, next::Point64{0, 0});

    const auto found = next::internal::find_next_adjacent_intersection(nodes.begin(), nodes.end());

    EXPECT_EQ(found, nodes.end());
}

TEST(Clipper2NextEngineIntersectionsTests, FindNextAdjacentIntersectionFindsLaterAdjacentPair) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    next::internal::active_edge_node third;
    next::internal::active_edge_node fourth;
    third.next_in_ael = &fourth;
    fourth.prev_in_ael = &third;
    next::internal::IntersectNodeList nodes;
    nodes.emplace_back(first, second, next::Point64{0, 0});
    nodes.emplace_back(third, fourth, next::Point64{0, 0});

    const auto found = next::internal::find_next_adjacent_intersection(nodes.begin(), nodes.end());

    ASSERT_NE(found, nodes.end());
    EXPECT_EQ(&found->first_edge(), &third);
    EXPECT_EQ(&found->second_edge(), &fourth);
}

TEST(Clipper2NextEngineIntersectionsTests, IntersectionsSortLeavesSingleNodeUntouched) {
    next::internal::active_edge_node first_edge;
    next::internal::active_edge_node second_edge;
    next::Point64 point{12, 34};
    std::vector<next::internal::IntersectNode> nodes;
    nodes.emplace_back(first_edge, second_edge, point);

    next::internal::sort_intersections(nodes);

    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(&nodes[0].first_edge(), &first_edge);
    EXPECT_EQ(&nodes[0].second_edge(), &second_edge);
    EXPECT_EQ(nodes[0].pt, point);
}

TEST(Clipper2NextEngineIntersectionsTests, AddIntersectionNodeStoresCalculatedPoint) {
    next::internal::active_edge_node first_edge;
    first_edge.bottom = {0, 10};
    first_edge.top_point = {10, 0};
    first_edge.current_x = 0;
    first_edge.dx = next::internal::get_dx(first_edge.bottom, first_edge.top_point);

    next::internal::active_edge_node second_edge;
    second_edge.bottom = {0, 0};
    second_edge.top_point = {10, 10};
    second_edge.current_x = 10;
    second_edge.dx = next::internal::get_dx(second_edge.bottom, second_edge.top_point);

    next::internal::IntersectNodeList nodes;

    next::internal::add_intersection_node(nodes, first_edge, second_edge, 0, 10);

    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(&nodes[0].first_edge(), &first_edge);
    EXPECT_EQ(&nodes[0].second_edge(), &second_edge);
    EXPECT_EQ(nodes[0].pt, next::Point64(5, 5));
}

TEST(Clipper2NextEngineIntersectionsTests, BuildIntersectionListFromSelRecordsInvertedPair) {
    next::internal::active_edge_node left_edge;
    left_edge.bottom = {0, 10};
    left_edge.top_point = {10, 0};
    left_edge.current_x = 20;
    left_edge.dx = next::internal::get_dx(left_edge.bottom, left_edge.top_point);

    next::internal::active_edge_node right_edge;
    right_edge.bottom = {0, 0};
    right_edge.top_point = {10, 10};
    right_edge.current_x = 10;
    right_edge.dx = next::internal::get_dx(right_edge.bottom, right_edge.top_point);

    left_edge.next_in_sel = &right_edge;
    left_edge.jump = &right_edge;
    right_edge.prev_in_sel = &left_edge;

    next::internal::active_edge_node* sorted_edges = &left_edge;
    next::internal::IntersectNodeList nodes;

    EXPECT_TRUE(next::internal::build_intersection_list_from_sel(sorted_edges, nodes, 0, 10));

    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(&nodes[0].first_edge(), &left_edge);
    EXPECT_EQ(&nodes[0].second_edge(), &right_edge);
    EXPECT_EQ(nodes[0].pt, next::Point64(5, 5));
    EXPECT_EQ(sorted_edges, &right_edge);
    EXPECT_EQ(right_edge.next_in_sel.get(), &left_edge);
    EXPECT_EQ(left_edge.prev_in_sel.get(), &right_edge);
    EXPECT_EQ(left_edge.next_in_sel.get(), nullptr);
}

TEST(Clipper2NextEngineIntersectionsTests,
     ContiguousUnitRunsPreserveSortedEdgesAndIntersectionGenerationOrder) {
    std::array<next::internal::active_edge_node, 8U> edge_storage;
    constexpr std::array<int64_t, 8U> top_xs{0, 30, 20, 10, 40, 50, 60, 70};
    std::vector<next::internal::active_edge_node*> edges;
    edges.reserve(edge_storage.size());
    for (std::size_t index = 0; index < edge_storage.size(); ++index) {
        auto& edge = edge_storage[index];
        edge.bottom = {static_cast<int64_t>(index * 10U), 10};
        edge.top_point = {top_xs[index], 0};
        edge.current_x = top_xs[index];
        edge.dx = next::internal::get_dx(edge.bottom, edge.top_point);
        edges.push_back(&edge);
    }

    std::vector<next::internal::active_edge_node*> scratch;
    next::internal::IntersectNodeList nodes;

    ASSERT_TRUE(next::internal::build_intersection_list_from_contiguous_unit_runs(
        edges, scratch, nodes, 0, 10));

    const std::array<next::internal::active_edge_node*, 8U> expected_edges{
        &edge_storage[0],
        &edge_storage[3],
        &edge_storage[2],
        &edge_storage[1],
        &edge_storage[4],
        &edge_storage[5],
        &edge_storage[6],
        &edge_storage[7],
    };
    EXPECT_TRUE(std::equal(edges.begin(), edges.end(), expected_edges.begin()));

    ASSERT_EQ(nodes.size(), 3U);
    EXPECT_EQ(&nodes[0].first_edge(), &edge_storage[2]);
    EXPECT_EQ(&nodes[0].second_edge(), &edge_storage[3]);
    EXPECT_EQ(&nodes[1].first_edge(), &edge_storage[1]);
    EXPECT_EQ(&nodes[1].second_edge(), &edge_storage[3]);
    EXPECT_EQ(&nodes[2].first_edge(), &edge_storage[1]);
    EXPECT_EQ(&nodes[2].second_edge(), &edge_storage[2]);
}

TEST(Clipper2NextEngineIntersectionsTests,
     ContiguousUnitRunsCheckBoundariesBetweenAdjacentMergePairs) {
    std::array<next::internal::active_edge_node, 4U> edge_storage;
    constexpr std::array<int64_t, 4U> top_xs{0, 10, 5, 15};
    std::vector<next::internal::active_edge_node*> edges;
    for (std::size_t index = 0; index < edge_storage.size(); ++index) {
        auto& edge = edge_storage[index];
        edge.bottom = {static_cast<int64_t>(index * 10U), 10};
        edge.top_point = {top_xs[index], 0};
        edge.current_x = top_xs[index];
        edge.dx = next::internal::get_dx(edge.bottom, edge.top_point);
        edges.push_back(&edge);
    }

    std::vector<next::internal::active_edge_node*> scratch;
    next::internal::IntersectNodeList nodes;

    ASSERT_TRUE(next::internal::build_intersection_list_from_contiguous_unit_runs(
        edges, scratch, nodes, 0, 10));

    const std::array<next::internal::active_edge_node*, 4U> expected_edges{
        &edge_storage[0], &edge_storage[2], &edge_storage[1], &edge_storage[3]};
    EXPECT_TRUE(std::equal(edges.begin(), edges.end(), expected_edges.begin()));
    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(&nodes[0].first_edge(), &edge_storage[1]);
    EXPECT_EQ(&nodes[0].second_edge(), &edge_storage[2]);
}

TEST(Clipper2NextEngineIntersectionsTests,
     ContiguousUnitRunsHandleOddTailAfterFirstPassInExactOrder) {
    std::array<next::internal::active_edge_node, 3U> edge_storage;
    constexpr std::array<int64_t, 3U> top_xs{20, 10, 30};
    std::vector<next::internal::active_edge_node*> edges;
    for (std::size_t index = 0; index < edge_storage.size(); ++index) {
        auto& edge = edge_storage[index];
        edge.bottom = {static_cast<int64_t>(index * 10U), 10};
        edge.top_point = {top_xs[index], 0};
        edge.current_x = top_xs[index];
        edge.dx = next::internal::get_dx(edge.bottom, edge.top_point);
        edges.push_back(&edge);
    }

    std::vector<next::internal::active_edge_node*> scratch;
    next::internal::IntersectNodeList nodes;

    ASSERT_TRUE(next::internal::build_intersection_list_from_contiguous_unit_runs(
        edges, scratch, nodes, 0, 10));

    const std::array<next::internal::active_edge_node*, 3U> expected_edges{
        &edge_storage[1], &edge_storage[0], &edge_storage[2]};
    EXPECT_TRUE(std::equal(edges.begin(), edges.end(), expected_edges.begin()));
    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(&nodes[0].first_edge(), &edge_storage[0]);
    EXPECT_EQ(&nodes[0].second_edge(), &edge_storage[1]);
}
