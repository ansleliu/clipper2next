#include "support/private/union_component_grouping.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

namespace {

[[nodiscard]] auto non_empty_components(
    const std::vector<std::vector<std::size_t>>& components)
    -> std::vector<std::vector<std::size_t>> {
    std::vector<std::vector<std::size_t>> result;
    for (const auto& component : components) {
        if (!component.empty()) { result.push_back(component); }
    }
    return result;
}

}  // namespace

TEST(UnionComponentGroupingTests, GroupsTransitiveBoundingBoxOverlaps) {
    const std::vector<next::Rect64> bounds{
        {0, 0, 10, 10},
        {20, 0, 30, 10},
        {9, 5, 21, 15},
        {100, 100, 110, 110},
    };

    const auto components = next::internal::group_union_components_by_bounds(bounds);

    const std::vector<std::vector<std::size_t>> expected{{0, 1, 2}, {3}};
    EXPECT_EQ(non_empty_components(components), expected);
}

TEST(UnionComponentGroupingTests, TreatsTouchingBoundsAsConnected) {
    const std::vector<next::Rect64> bounds{
        {0, 0, 10, 10},
        {10, 10, 20, 20},
        {21, 21, 30, 30},
    };

    const auto components = next::internal::group_union_components_by_bounds(bounds);

    const std::vector<std::vector<std::size_t>> expected{{0, 1}, {2}};
    EXPECT_EQ(non_empty_components(components), expected);
}

TEST(UnionComponentGroupingTests, IgnoresInvalidBoundsAndKeepsStableComponentSlots) {
    const std::vector<next::Rect64> bounds{
        {0, 0, 10, 10},
        next::Rect64::invalid_rect(),
        {30, 30, 40, 40},
    };

    const auto components = next::internal::group_union_components_by_bounds(bounds);

    ASSERT_EQ(components.size(), bounds.size());
    EXPECT_EQ(components[0], std::vector<std::size_t>({0}));
    EXPECT_TRUE(components[1].empty());
    EXPECT_EQ(components[2], std::vector<std::size_t>({2}));
}
