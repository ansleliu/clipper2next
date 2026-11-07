#include "support/path_equivalence.h"
#include "clip/private/clip_execution_strategy.h"
#include "clipper2next/clip.h"
#include <algorithm>
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

TEST(Clipper2NextPolyTreeTests, RectangleCertificateRejectsCrossedCornerOrder) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{0,0},{20,20},{20,0},{0,20}}};
    const next::Paths64 expected{{{0,0},{10,10},{0,20}},{{20,0},{20,20},{10,10}}};
    auto raw = next::clip_tree(request);
    next::clip_tree64_result into;
    next::clip_tree_into(request, into);
    auto checked = next::clip_tree_checked(request);
    ASSERT_TRUE(checked.has_value());
    for (const auto* result : {&raw, &into, &*checked}) {
        ASSERT_EQ(result->tree.count(), 2U);
        next::Paths64 polygons;
        for (const auto node : result->tree.children(result->tree.root())) {
            EXPECT_FALSE(result->tree.is_hole(node));
            EXPECT_EQ(result->tree.count(node), 0U);
            polygons.push_back(result->tree.polygon(node));
        }
        EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(expected, polygons));
    }
}

TEST(Clipper2NextPolyTreeTests, RectangleCertificateNormalizesClockwiseShellsExactly) {
    for (const auto base : {std::int64_t{}, std::int64_t{1} << 60, -(std::int64_t{1} << 60)}) {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Union;
        request.fill_rule = next::FillRule::NonZero;
        request.subjects = {{{base,base},{base,base+20},{base+20,base+20},{base+20,base}}};
        const next::Paths64 expected{{{base,base},{base+20,base},{base+20,base+20},{base,base+20}}};
        const auto result = next::clip_tree(request);
        ASSERT_EQ(result.tree.count(), 1U);
        const auto child = result.tree.child(result.tree.root(), 0);
        EXPECT_FALSE(result.tree.is_hole(child));
        ASSERT_EQ(result.tree.polygon(child).size(), 4U);
        EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(expected, next::Paths64{result.tree.polygon(child)}));
    }
}

TEST(Clipper2NextPolyTreeTests, RectangleCertificateKeepsOppositeWindingHolesAtLargeCoordinates) {
    for (const auto base : {std::int64_t{}, std::int64_t{1} << 60, -(std::int64_t{1} << 60)}) {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Union;
        request.fill_rule = next::FillRule::NonZero;
        request.subjects = {{{base,base},{base+20,base},{base+20,base+20},{base,base+20}},
                            {{base+5,base+5},{base+5,base+15},{base+15,base+15},{base+15,base+5}}};
        const auto result = next::clip_tree(request);
        ASSERT_EQ(result.tree.count(), 1U);
        const auto shell = result.tree.child(result.tree.root(), 0);
        ASSERT_EQ(result.tree.count(shell), 1U);
        const auto hole = result.tree.child(shell, 0);
        EXPECT_FALSE(result.tree.is_hole(shell));
        EXPECT_TRUE(result.tree.is_hole(hole));
        EXPECT_EQ(result.tree.depth(hole), 2U);
        EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(next::Paths64{request.subjects[0]}, next::Paths64{result.tree.polygon(shell)}));
        EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(next::Paths64{request.subjects[1]}, next::Paths64{result.tree.polygon(hole)}));
    }
}

TEST(Clipper2NextPolyTreeTests, RectangleCertificateLeavesSharedEdgesToTheUnionEngine) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{0,0},{20,0},{20,20},{0,20}},{{0,0},{10,0},{10,20},{0,20}}};
    next::clip_tree64_result expected;
    next::internal::execute_clip_tree_into_validated(request, expected.tree, expected.open);
    const auto actual = next::clip_tree(request);
    ASSERT_EQ(expected.tree.count(), 1U);
    ASSERT_EQ(actual.tree.count(), 1U);
    const auto& expected_path = expected.tree.polygon(expected.tree.child(expected.tree.root(), 0));
    const auto& actual_path = actual.tree.polygon(actual.tree.child(actual.tree.root(), 0));
    EXPECT_EQ(actual_path.size(), expected_path.size());
    EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(next::Paths64{expected_path}, next::Paths64{actual_path}));
}
