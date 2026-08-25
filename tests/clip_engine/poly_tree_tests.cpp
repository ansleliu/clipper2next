#include "clipper2next/polygon/poly_tree.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextPolyTreeTests, TracksHierarchyHoleStateAndArea) {
    next::PolyTree64 tree;
    const auto outer =
        tree.add_child(tree.root(), next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    const auto hole = tree.add_child(outer, next::Path64{{2, 2}, {2, 8}, {8, 8}, {8, 2}});

    EXPECT_EQ(tree.count(), 1U);
    EXPECT_EQ(tree.depth(outer), 1U);
    EXPECT_EQ(tree.depth(hole), 2U);
    EXPECT_FALSE(tree.is_hole(outer));
    EXPECT_TRUE(tree.is_hole(hole));
    EXPECT_NEAR(tree.area(), 64.0, 0.001);
}

TEST(Clipper2NextPolyTreeTests, ReservePreservesRootAndRelationships) {
    next::PolyTree64 tree;
    tree.reserve(8);
    tree.reserve_children(tree.root(), 2);

    const auto first =
        tree.add_child(tree.root(), next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    const auto second =
        tree.add_child(tree.root(), next::Path64{{20, 20}, {30, 20}, {30, 30}, {20, 30}});
    const auto nested =
        tree.add_child(first, next::Path64{{2, 2}, {8, 2}, {8, 8}, {2, 8}});

    EXPECT_EQ(tree.count(tree.root()), 2U);
    EXPECT_EQ(tree.child(tree.root(), 0), first);
    EXPECT_EQ(tree.child(tree.root(), 1), second);
    EXPECT_EQ(tree.parent(nested), first);
    EXPECT_TRUE(tree.is_hole(nested));
}

TEST(Clipper2NextPolyTreeTests, DoubleTreeScalesIntegerChildren) {
    next::PolyTreeD tree;
    tree.set_scale(0.5);
    const auto child =
        tree.add_child(tree.root(), next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}});

    EXPECT_DOUBLE_EQ(tree.scale(), 0.5);
    ASSERT_EQ(tree.polygon(child).size(), 4U);
    EXPECT_DOUBLE_EQ(tree.polygon(child)[1].x, 5.0);
}

TEST(Clipper2NextPolyTreeTests, DoubleTreeRejectsInvalidScaleWithoutAddingChild) {
    next::PolyTreeD tree;
    tree.set_scale(0.0);

    try {
        (void)tree.add_child(tree.root(), next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        FAIL() << "expected clipper_error";
    } catch (const next::clipper_error& error) {
        EXPECT_EQ(error.code(), next::clipper_error_code::scale_out_of_range);
    }
    EXPECT_EQ(tree.count(), 0U);
}
