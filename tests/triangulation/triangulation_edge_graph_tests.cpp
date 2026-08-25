#include <gtest/gtest.h>

#include "triangulation/private/triangulation_edge_graph.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationEdgeGraphTests, EdgeGraphLinksTwoVertices) {
    next::internal::triangulation_vertex first{{0, 0}};
    next::internal::triangulation_vertex second{{10, 0}};
    const auto edge = next::internal::make_triangulation_edge(&first, &second);

    EXPECT_EQ(edge.first, &first);
    EXPECT_EQ(edge.second, &second);
}
