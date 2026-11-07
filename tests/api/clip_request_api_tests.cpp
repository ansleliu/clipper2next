#include "clipper2next/clip.h"
#include "clipper2next/geometry.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <cfenv>
#include "clipper2next/offset/operations.h"
#include "clipper2next/rectclip/request.h"
#include "clipper2next/triangulation/request.h"

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(DirectMaterialization, OwningPathsRetainLiveInnerStorage) {
    const next::Paths64 input{{{0, 0}, {90, 0}, {100, 50}, {70, 100}, {0, 80}}};
    auto check = [&](auto run) {
        next::Paths64 output(1);
        output.reserve(1000);
        output[0].reserve(10000);
        auto* outer = output.data();
        auto* inner = output[0].data();
        run(output);
        ASSERT_FALSE(output.empty());
        EXPECT_EQ(output.data(), outer);
        EXPECT_EQ(output[0].data(), inner);
        const auto expected = output;
        std::vector<const next::Point64*> inner_addresses;
        for (const auto& path : output) { inner_addresses.push_back(path.data()); }
        run(output);
        EXPECT_EQ(output, expected);
        for (std::size_t i = 0; i < output.size(); ++i) {
            EXPECT_EQ(output[i].data(), inner_addresses[i]);
        }
        EXPECT_EQ(output.data(), outer);
        EXPECT_EQ(output[0].data(), inner);
    };
    check([&](auto& output) {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Union;
        request.subjects = input;
        next::paths64_result result;
        result.closed.swap(output);
        next::clip_into(request, result);
        output.swap(result.closed);
    });
    check([&](auto& output) {
        next::offset_request64 request;
        request.paths = input;
        request.delta = 3;
        next::paths64_result result;
        result.closed.swap(output);
        next::offset_into(request, result);
        output.swap(result.closed);
    });
    check([&](auto& output) {
        next::rect_clip_request64 request;
        request.rect = {10, 10, 80, 80};
        request.paths = input;
        next::rect_clip_result64 result;
        result.paths.swap(output);
        next::rect_clip_into(request, result);
        output.swap(result.paths);
    });
    check([&](auto& output) {
        next::triangulation_request64 request;
        request.paths = input;
        next::triangulation_result64 result;
        result.triangles.swap(output);
        next::triangulate_into(request, result);
        output.swap(result.triangles);
    });
}

TEST(DirectMaterialization, TreeReusesEveryNestedPolygonAndChildrenBuffer) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::EvenOdd;
    request.subjects = {{{0, 0}, {100, 0}, {95, 100}, {0, 100}},
                        {{10, 10}, {80, 10}, {75, 80}, {10, 80}},
                        {{20, 20}, {60, 20}, {55, 60}, {20, 60}}};
    auto result = next::clip_tree(request);
    const auto expected = result.tree;
    std::vector<const next::Point64*> points;
    std::vector<const next::PolyTree64::node_id*> children;
    for (std::size_t i = 0; i < 4; ++i) {
        points.push_back(result.tree.polygon({i}).data());
        children.push_back(result.tree.children({i}).data());
    }
    next::clip_tree_into(request, result);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(result.tree.polygon({i}), expected.polygon({i}));
        EXPECT_EQ(result.tree.parent({i}), expected.parent({i}));
        EXPECT_EQ(result.tree.depth({i}), expected.depth({i}));
        EXPECT_EQ(result.tree.polygon({i}).data(), points[i]);
        EXPECT_EQ(result.tree.children({i}).data(), children[i]);
    }
}

TEST(Clipper2NextClipRequestApiTests, ClipReturnsClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::clip(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClipRequestApiTests, ClipRequestCanSelectPreciseIntersectionPolicy) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.options.intersection_policy = next::predicate_policy{next::precision_mode::precise};
    request.subjects = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };
    request.clips = next::Paths64{
        test::path64({50, -25, 125, -25, 125, 75, 50, 75}),
    };

    const auto result = next::clip_checked(request);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().closed.size(), 1U);
    EXPECT_EQ(next::bounds(result.value().closed), next::Rect64(50, 0, 100, 75));
}

TEST(Clipper2NextClipRequestApiTests, ClipUnionWithEmptySubjectsReturnsClipClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.clips = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::clip(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed[0].size(), 4U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClipRequestApiTests, ClipXorWithEmptySubjectsReturnsClipClosedPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Xor;
    request.fill_rule = next::FillRule::NonZero;
    request.clips = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::clip(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed[0].size(), 4U);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClipRequestApiTests, ClipReturnsOpenPaths) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.open_subjects = {{{0, 50}, {100, 50}}};
    request.clips = {{{25, 25}, {75, 25}, {75, 75}, {25, 75}}};

    const auto result = next::clip(request);

    EXPECT_TRUE(result.closed.empty());
    ASSERT_EQ(result.open.size(), 1U);
    EXPECT_EQ(result.open[0], next::Path64({{25, 50}, {75, 50}}));
}

TEST(Clipper2NextClipRequestApiTests, ClipTreeReturnsHierarchy) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {
        {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        {{2, 2}, {2, 8}, {8, 8}, {8, 2}},
    };

    const auto result = next::clip_tree(request);

    EXPECT_TRUE(result.open.empty());
    ASSERT_EQ(result.tree.count(), 1U);
    const auto outer = result.tree.child(result.tree.root(), 0);
    ASSERT_EQ(result.tree.count(outer), 1U);
    const auto hole = result.tree.child(outer, 0);
    EXPECT_TRUE(result.tree.is_hole(hole));
    EXPECT_NEAR(result.tree.area(), 64.0, 0.001);
}

TEST(Clipper2NextClipRequestApiTests, ClipExecutionIsIndependentOfCallerRoundingMode) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{0, 0}, {1, 2}, {2, 0}}};
    request.clips = {{{-10, 1}, {10, 1}, {10, 3}, {-10, 3}}};

    const auto original_mode = std::fegetround();
    ASSERT_NE(original_mode, -1);
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const auto expected = next::clip(request);

    for (const auto mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        EXPECT_EQ(next::clip(request).closed, expected.closed);
        EXPECT_EQ(std::fegetround(), mode);
    }
    ASSERT_EQ(std::fesetround(original_mode), 0);
}

TEST(DirectMaterialization, PreparedFastPathsAndTreeKeepLiveBuffers) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.subjects = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    request.clips = {{{25, 25}, {200, 25}, {200, 200}, {25, 200}}};
    const auto prepared = next::prepare_clip_request(request);
    auto result = next::clip(prepared);
    result.closed.reserve(1000);
    result.closed[0].reserve(10000);
    const auto* outer = result.closed.data();
    const auto* inner = result.closed[0].data();
    const auto expected = result.closed;
    next::clip_into(prepared, result);
    EXPECT_EQ(result.closed, expected);
    EXPECT_EQ(result.closed.data(), outer);
    EXPECT_EQ(result.closed[0].data(), inner);
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.clips.clear();
    auto tree = next::clip_tree(request);
    const auto node = tree.tree.child(tree.tree.root(), 0);
    const auto* polygon = tree.tree.polygon(node).data();
    const auto* children = tree.tree.children(tree.tree.root()).data();
    next::clip_tree_into(request, tree);
    EXPECT_EQ(tree.tree.polygon(node).data(), polygon);
    EXPECT_EQ(tree.tree.children(tree.tree.root()).data(), children);
    EXPECT_EQ(tree.tree.polygon(node), request.subjects.front());
}
