#include "support/private/storage/topology_store.h"

#include <gtest/gtest.h>

#include <vector>

namespace next = clipper2next;

namespace {

struct test_node {
    int value{};
};

struct test_topology_tag;

}  // namespace

TEST(Clipper2NextTopologyStoreTests, StableHandleSurvivesGrowth) {
    next::internal::topology_store<test_node, test_topology_tag> store;
    const auto first = store.emplace(test_node{7});

    for (int value = 0; value < 128; ++value) {
        static_cast<void>(store.emplace(test_node{value}));
    }

    ASSERT_NE(store.get(first), nullptr);
    EXPECT_EQ(store.ref(first).value, 7);
}

TEST(Clipper2NextTopologyStoreTests, StaleGenerationIsRejectedAfterErase) {
    next::internal::topology_store<test_node, test_topology_tag> store;
    const auto first = store.emplace(test_node{3});

    EXPECT_TRUE(store.erase(first));
    EXPECT_EQ(store.get(first), nullptr);

    const auto second = store.emplace(test_node{4});
    EXPECT_NE(first.generation, second.generation);
    EXPECT_EQ(store.get(first), nullptr);
    ASSERT_NE(store.get(second), nullptr);
    EXPECT_EQ(store.ref(second).value, 4);
}

TEST(Clipper2NextTopologyStoreTests, IterationOrderIsDeterministic) {
    next::internal::topology_store<test_node, test_topology_tag> store;
    const auto first = store.emplace(test_node{1});
    static_cast<void>(store.emplace(test_node{2}));
    const auto third = store.emplace(test_node{3});
    EXPECT_TRUE(store.erase(first));
    static_cast<void>(store.emplace(test_node{4}));

    std::vector<int> values;
    store.for_each([&](auto, const test_node& node) { values.push_back(node.value); });

    EXPECT_EQ(values, (std::vector<int>{4, 2, 3}));
    EXPECT_EQ(store.ref(third).value, 3);
}
