#include "clipper2next/core/path.h"
#include "clipper2next/core/point.h"
#include "clipper2next/polygon/tree.h"
#include "clipper2next/core/rect.h"

#include <gtest/gtest.h>
#include <sstream>

namespace next = clipper2next;

TEST(Clipper2NextValueTypeApiTests, PointAliasesAndValueHelpersAreNonMutating) {
    const next::point64 point{1, 2};
    const auto negated = next::negated(point);

    EXPECT_EQ(negated, next::point64(-1, -2));
}

TEST(Clipper2NextValueTypeApiTests, RectFreeFunctionsMirrorValueSemantics) {
    const next::rect64 rect{0, 0, 10, 20};
    const auto wider = next::with_width(rect, 12);
    const auto taller = next::with_height(rect, 24);
    const auto doubled = next::scaled(rect, 2.0);

    EXPECT_EQ(next::width(rect), 10);
    EXPECT_EQ(next::height(rect), 20);
    EXPECT_EQ(next::midpoint(rect), next::point64(5, 10));
    EXPECT_EQ(next::as_path(rect).size(), 4U);
    EXPECT_TRUE(next::contains(rect, next::point64{5, 5}));
    EXPECT_TRUE(next::contains(next::rect64{-1, -1, 11, 21}, rect));
    EXPECT_FALSE(next::is_empty(rect));
    EXPECT_TRUE(next::intersects(rect, next::rect64{5, 5, 15, 15}));
    EXPECT_EQ(next::union_bounds(rect, next::rect64{-5, 5, 5, 25}), next::rect64(-5, 0, 10, 25));
    EXPECT_EQ(wider.right, 12);
    EXPECT_EQ(taller.bottom, 24);
    EXPECT_EQ(doubled, next::rect64(0, 0, 20, 40));
}

TEST(Clipper2NextValueTypeApiTests, RectScaledRoundsIntegralCoordinatesToEven) {
    const next::rect64 rect{0, 0, 3, 5};

    EXPECT_EQ(next::scaled(rect, 0.5), next::rect64(0, 0, 2, 2));
}

TEST(Clipper2NextValueTypeApiTests, PolygonTreeUsesStableNodeIds) {
    next::polygon_tree64 tree;
    const auto root = tree.root();
    const auto outer = tree.add_child(root, next::path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    const auto hole = tree.add_child(outer, next::path64{{2, 2}, {2, 8}, {8, 8}, {8, 2}});

    ASSERT_EQ(tree.children(root).size(), 1U);
    EXPECT_EQ(tree.children(root).front(), outer);
    EXPECT_EQ(tree.children(outer).front(), hole);
    EXPECT_EQ(tree.depth(outer), 1U);
    EXPECT_TRUE(tree.is_hole(hole));
    EXPECT_NEAR(tree.area(root), 64.0, 0.001);
    EXPECT_EQ(tree.polygon(outer).size(), 4U);
}

TEST(Clipper2NextValueTypeApiTests, PathWritersRemainExplicit) {
    const next::path64 path{{1, 2}, {3, 4}};

    std::ostringstream stream;
    next::write_path(stream, path);

    EXPECT_NE(stream.str().find("1,2"), std::string::npos);
}
