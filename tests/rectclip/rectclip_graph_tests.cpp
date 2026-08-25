#include <gtest/gtest.h>

#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_edges.h"
#include "rectclip/private/rectclip_execution_context.h"
#include "rectclip/private/rectclip_graph.h"
#include "rectclip/private/rectclip_path_builder.h"

#include <array>
#include <type_traits>
#include <utility>

namespace next = clipper2next;

TEST(Clipper2NextRectClipGraphTests, GraphStorageDoesNotExposeRawNodePointerFields) {
    using node = next::internal::rectclip_node;
    using node_list = next::internal::rectclip_node_list;
    using execution_context = next::internal::rectclip_execution_context;

    static_assert(!std::is_pointer_v<decltype(std::declval<node&>().next)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<node&>().prev)>);
    static_assert(!std::is_pointer_v<typename node_list::value_type>);
    static_assert(std::is_lvalue_reference_v<decltype(std::declval<execution_context&>().add(
                      std::declval<next::Point64>(), false))>);
}

TEST(Clipper2NextRectClipGraphTests, ContextNodePoolKeepsReferencesStable) {
    next::internal::rectclip_context storage{{0, 0, 10, 10}};
    next::internal::rectclip_execution_context execution{storage};

    auto& first = execution.add({1, 1}, true);
    for (int index = 0; index < 64; ++index) {
        static_cast<void>(execution.add({index + 2, index + 2}));
    }

    EXPECT_EQ(first.pt, next::Point64(1, 1));
    EXPECT_EQ(storage.op_container.size(), 65U);
}

TEST(Clipper2NextRectClipGraphTests, ContextNodePoolClearReleasesOwnedNodes) {
    next::internal::rectclip_context storage{{0, 0, 10, 10}};
    next::internal::rectclip_execution_context execution{storage};
    static_cast<void>(execution.add({1, 1}, true));

    storage.op_container.clear();
    storage.results.clear();

    EXPECT_TRUE(storage.op_container.empty());
    EXPECT_TRUE(storage.results.empty());
}

TEST(Clipper2NextRectClipGraphTests, RectClipNodeStartsDetached) {
    const next::internal::rectclip_node node{{1, 2}};

    EXPECT_EQ(node.pt.x, 1);
    EXPECT_EQ(node.pt.y, 2);
    EXPECT_EQ(node.owner_index, 0U);
    EXPECT_EQ(node.edge, nullptr);
    EXPECT_EQ(node.next.get(), nullptr);
    EXPECT_EQ(node.prev.get(), nullptr);
}

TEST(Clipper2NextRectClipGraphTests, ContextStoresRectangleBoundaryAsFixedArray) {
    using rect_path_type = std::remove_cvref_t<
        decltype(std::declval<next::internal::rectclip_context&>().rect_as_path)>;
    static_assert(std::is_same_v<rect_path_type, std::array<next::Point64, 4>>);

    const next::internal::rectclip_context context{{0, 10, 20, 30}};

    EXPECT_EQ(context.rect_as_path[0], next::Point64(0, 10));
    EXPECT_EQ(context.rect_as_path[1], next::Point64(20, 10));
    EXPECT_EQ(context.rect_as_path[2], next::Point64(20, 30));
    EXPECT_EQ(context.rect_as_path[3], next::Point64(0, 30));
}

TEST(Clipper2NextRectClipGraphTests, EdgeAttachAndDetachUpdatesNodeBackPointer) {
    next::internal::rectclip_node node{{0, 0}};
    next::internal::rectclip_node_list edge;

    next::internal::add_to_edge(edge, &node);
    next::internal::add_to_edge(edge, &node);

    ASSERT_EQ(edge.size(), 1U);
    EXPECT_EQ(edge[0].get(), &node);
    EXPECT_EQ(node.edge, &edge);

    next::internal::uncouple_edge(&node);

    EXPECT_EQ(edge[0].get(), nullptr);
    EXPECT_EQ(node.edge, nullptr);
}

TEST(Clipper2NextRectClipGraphTests, TidyEdgesLeavesEmptyEdgeListsEmpty) {
    next::internal::rectclip_node_list clockwise;
    next::internal::rectclip_node_list counter_clockwise;
    next::internal::rectclip_node_list results;

    next::internal::tidy_edges(0, clockwise, counter_clockwise, results);

    EXPECT_TRUE(clockwise.empty());
    EXPECT_TRUE(counter_clockwise.empty());
    EXPECT_TRUE(results.empty());
}

TEST(Clipper2NextRectClipGraphTests, SetNewOwnerPropagatesThroughCircularPath) {
    next::internal::rectclip_node first{{0, 0}};
    next::internal::rectclip_node second{{10, 0}};
    first.next = &second;
    first.prev = &second;
    second.next = &first;
    second.prev = &first;

    next::internal::set_new_owner(&first, 7);

    EXPECT_EQ(first.owner_index, 7U);
    EXPECT_EQ(second.owner_index, 7U);
}

TEST(Clipper2NextRectClipGraphTests, BuildLinePathPreservesTwoNodeLinks) {
    next::internal::rectclip_node first{{0, 0}};
    next::internal::rectclip_node second{{10, 0}};
    first.next = &second;
    first.prev = &second;
    second.next = &first;
    second.prev = &first;
    auto* start = &first;

    const auto path = next::internal::build_line_path(start);

    ASSERT_EQ(path.size(), 2U);
    EXPECT_EQ(first.next.get(), &second);
    EXPECT_EQ(first.prev.get(), &second);
    EXPECT_EQ(second.next.get(), &first);
    EXPECT_EQ(second.prev.get(), &first);
}

TEST(Clipper2NextRectClipGraphTests, BuildPolygonPathExtractsCircularPath) {
    next::internal::rectclip_node first{{0, 0}};
    next::internal::rectclip_node second{{10, 0}};
    next::internal::rectclip_node third{{0, 10}};
    first.next = &second;
    first.prev = &third;
    second.next = &third;
    second.prev = &first;
    third.next = &first;
    third.prev = &second;
    auto* start = &first;

    const auto path = next::internal::build_polygon_path(start);

    ASSERT_EQ(path.size(), 3U);
    EXPECT_EQ(path[0], next::Point64(0, 0));
    EXPECT_EQ(path[1], next::Point64(10, 0));
    EXPECT_EQ(path[2], next::Point64(0, 10));
}

TEST(Clipper2NextRectClipGraphTests, ExecutionContextAppendsCircularNodes) {
    next::internal::rectclip_context storage{{0, 0, 10, 10}};
    next::internal::rectclip_execution_context context{storage};

    auto& first = context.add({1, 1}, false);
    auto& second = context.add({2, 2}, false);

    EXPECT_EQ(first.next.get(), &second);
    EXPECT_EQ(second.prev.get(), &first);
    EXPECT_EQ(storage.results.size(), 1U);
}
