#include <gtest/gtest.h>

#include "triangulation/private/triangulation_graph.h"
#include "triangulation/private/triangulation_context.h"

#include <type_traits>
#include <utility>

namespace next = clipper2next;

TEST(Clipper2NextTriangulationGraphTests, RuntimeTopologyViewsDoNotExposeRawPointerFields) {
    using next::internal::triangulation_context;
    using next::internal::triangulation_edge;
    using next::internal::triangulation_edge_list;
    using next::internal::triangulation_triangle;
    using next::internal::triangulation_triangle_list;
    using next::internal::triangulation_vertex;
    using next::internal::triangulation_vertex_list;

    static_assert(!std::is_pointer_v<triangulation_edge_list::value_type>);
    static_assert(!std::is_pointer_v<triangulation_vertex_list::value_type>);
    static_assert(!std::is_pointer_v<triangulation_triangle_list::value_type>);
    static_assert(!std::is_pointer_v<decltype(std::declval<triangulation_edge>().first)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<triangulation_edge>().second)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<triangulation_edge>().vL)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<triangulation_edge>().triA)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<triangulation_edge>().nextE)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<triangulation_triangle>().edges[0])>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<triangulation_context>().pending_delaunay)::value_type>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<triangulation_context>().horizontal_edges)::value_type>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<triangulation_context>().local_minima)::value_type>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<triangulation_context>().lowermost_vertex)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<triangulation_context>().first_active)>);
}

TEST(Clipper2NextTriangulationGraphTests, IndexGraphKeepsStableVertexReferences) {
    next::internal::triangulation_graph graph;

    const auto first = graph.add_vertex({0, 0});
    const auto second = graph.add_vertex({10, 0});

    for (int index = 0; index < 128; ++index) { (void)graph.add_vertex({index, index + 1}); }

    EXPECT_EQ(graph.vertex(first).point, next::Point64(0, 0));
    EXPECT_EQ(graph.vertex(second).point, next::Point64(10, 0));
    EXPECT_EQ(graph.vertex_count(), 130U);
}

TEST(Clipper2NextTriangulationGraphTests, IndexGraphConnectsEdgesAndTrianglesByIds) {
    next::internal::triangulation_graph graph;
    const auto first = graph.add_vertex({0, 0});
    const auto second = graph.add_vertex({10, 0});
    const auto third = graph.add_vertex({0, 10});

    const auto edge_a =
        graph.add_edge(first, second, next::internal::triangulation_edge_kind::ascend);
    const auto edge_b =
        graph.add_edge(second, third, next::internal::triangulation_edge_kind::loose);
    const auto edge_c =
        graph.add_edge(third, first, next::internal::triangulation_edge_kind::descend);
    const auto triangle = graph.add_triangle(edge_a, edge_b, edge_c);

    EXPECT_EQ(graph.edge(edge_a).first, first);
    EXPECT_EQ(graph.edge(edge_a).second, second);
    EXPECT_EQ(graph.edge(edge_a).kind, next::internal::triangulation_edge_kind::ascend);
    EXPECT_EQ(graph.triangle(triangle).edges[0], edge_a);
    EXPECT_EQ(graph.triangle_count(), 1U);
}
