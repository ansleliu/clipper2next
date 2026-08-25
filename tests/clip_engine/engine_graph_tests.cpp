#include "clip/engine/private/engine_graph.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace next = clipper2next;

TEST(Clipper2NextEngineGraphTests, HandleTypesAreStronglyTyped) {
    static_assert(!std::is_convertible_v<next::internal::edge_id, std::uint32_t>);
    static_assert(!std::is_convertible_v<next::internal::vertex_id, std::uint32_t>);
    static_assert(!std::is_convertible_v<next::internal::output_record_id, std::uint32_t>);
    static_assert(!std::is_convertible_v<next::internal::output_point_id, std::uint32_t>);
}

TEST(Clipper2NextEngineGraphTests, EdgeHandlesRemainStableAcrossPoolGrowth) {
    next::internal::engine_graph graph;
    const auto first = graph.create_edge({0, 0}, {10, 10});

    for (auto index = 0; index < 128; ++index) {
        static_cast<void>(graph.create_edge({index, index}, {index + 1, index + 2}));
    }

    ASSERT_TRUE(graph.contains(first));
    EXPECT_EQ(graph.edge(first).bottom, next::Point64(0, 0));
    EXPECT_EQ(graph.edge(first).top_point, next::Point64(10, 10));
}

TEST(Clipper2NextEngineGraphTests, ClearInvalidatesOldHandlesByGeneration) {
    next::internal::engine_graph graph;
    const auto edge = graph.create_edge({0, 0}, {10, 10});

    graph.clear();

    EXPECT_FALSE(graph.contains(edge));
}

TEST(Clipper2NextEngineGraphTests, OutputRingTraversesThroughHandles) {
    next::internal::engine_graph graph;
    const auto record = graph.create_output_record();
    static_cast<void>(graph.append_output_point(record, {0, 0}));
    static_cast<void>(graph.append_output_point(record, {10, 0}));
    static_cast<void>(graph.append_output_point(record, {10, 10}));

    const auto ring = graph.output_ring(record);

    ASSERT_EQ(ring.size(), 3U);
    EXPECT_EQ(ring[0], next::Point64(0, 0));
    EXPECT_EQ(ring[1], next::Point64(10, 0));
    EXPECT_EQ(ring[2], next::Point64(10, 10));
}

TEST(Clipper2NextEngineGraphTests, GraphApisDoNotExposeRawPointers) {
    using graph_type = next::internal::engine_graph;

    static_assert(!std::is_pointer_v<decltype(std::declval<graph_type&>().edge(
                      std::declval<next::internal::edge_id>()))>);
    static_assert(!std::is_pointer_v<decltype(std::declval<graph_type&>().output_ring(
                      std::declval<next::internal::output_record_id>()))>);
}
