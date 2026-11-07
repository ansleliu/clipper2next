#include <gtest/gtest.h>

#include "triangulation/private/triangulation_edge_graph.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationEdgeGraphTests, EdgeGraphLinksTwoVertices) {
    next::internal::triangulation_vertex first{{0, 0}};
    next::internal::triangulation_vertex second{{10, 0}};
    auto edge = next::internal::make_triangulation_edge(&first, &second);

    EXPECT_EQ(edge.first, &first);
    EXPECT_EQ(edge.second, &second);
    next::internal::triangulation_triangle previous_face{&edge, &edge, &edge};
    edge.triA = &previous_face;
    edge.triB = &previous_face;
    edge.first_event = 17U;
    edge.second_event = 23U;
    edge.isActive = true;
    edge.nextE = &edge;
    edge.prevE = &edge;
    next::internal::initialize_triangulation_edge(
        edge, &second, &first, next::internal::triangulation_edge_kind::descend);
    EXPECT_EQ(edge.first, &second);
    EXPECT_EQ(edge.second, &first);
    EXPECT_EQ(edge.vL, &first);
    EXPECT_EQ(edge.vR, &second);
    EXPECT_EQ(edge.vB, &first);
    EXPECT_EQ(edge.vT, &second);
    EXPECT_EQ(edge.kind, next::internal::triangulation_edge_kind::descend);
    EXPECT_EQ(edge.triA, nullptr);
    EXPECT_EQ(edge.triB, nullptr);
    EXPECT_EQ(edge.first_event, next::internal::no_boundary_event);
    EXPECT_EQ(edge.second_event, next::internal::no_boundary_event);
    EXPECT_FALSE(edge.isActive);
    EXPECT_EQ(edge.nextE, nullptr);
    EXPECT_EQ(edge.prevE, nullptr);
}
