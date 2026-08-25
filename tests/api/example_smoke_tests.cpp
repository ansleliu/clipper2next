#include "clipper2next/clipper.h"
#include "clipper2next/offset/builder.h"
#include "clipper2next/rectclip.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextExampleSmokeTests, RandomClippingStyleRequestHandlesClosedAndOpenOutputs) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::EvenOdd;
    request.subjects = {{{0, 0}, {80, 0}, {80, 80}, {0, 80}}};
    request.open_subjects = {{{-20, 40}, {100, 40}}};
    request.clips = {{{20, 20}, {60, 20}, {60, 60}, {20, 60}}};

    const auto result = next::clip(request);

    ASSERT_EQ(result.closed.size(), 1U);
    ASSERT_EQ(result.open.size(), 1U);
    EXPECT_EQ(result.open.front(), next::Path64({{20, 40}, {60, 40}}));
}

TEST(Clipper2NextExampleSmokeTests, InflateAndVariableOffsetStyleRequestsProduceGeometry) {
    const next::Path64 open_path{{0, 0}, {40, 0}, {80, 40}, {120, 40}};

    const auto constant_offset = next::offset_builder{}
                                     .delta(10.0)
                                     .join(next::JoinType::Round)
                                     .end(next::EndType::Round)
                                     .add(open_path)
                                     .execute();

    ASSERT_FALSE(constant_offset.empty());
    EXPECT_GT(next::area(constant_offset), 0.0);

    const auto variable_offset =
        next::offset_builder{}
            .join(next::JoinType::Miter)
            .end(next::EndType::Round)
            .delta_callback([](const next::Path64&,
                               const next::PathD&,
                               std::size_t current_index,
                               std::size_t) { return 5.0 + static_cast<double>(current_index); })
            .add(open_path)
            .execute();

    ASSERT_FALSE(variable_offset.empty());
    EXPECT_GT(next::area(variable_offset), 0.0);
}

TEST(Clipper2NextExampleSmokeTests, PolygonSamplesStyleTreeAndRectClipProduceGeometry) {
    next::clip_request64 tree_request;
    tree_request.clip_type = next::ClipType::Union;
    tree_request.fill_rule = next::FillRule::NonZero;
    tree_request.subjects = {
        {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
        {{25, 25}, {25, 75}, {75, 75}, {75, 25}},
    };

    const auto tree_result = next::clip_tree(tree_request);
    ASSERT_EQ(tree_result.tree.count(), 1U);
    EXPECT_EQ(tree_result.tree.count(tree_result.tree.child(tree_result.tree.root(), 0)), 1U);

    next::rect_clip_request64 rect_request;
    rect_request.rect = {10, 10, 90, 90};
    rect_request.paths = tree_request.subjects;

    const auto rect_result = next::rect_clip(rect_request);
    ASSERT_FALSE(rect_result.paths.empty());
}

TEST(Clipper2NextExampleSmokeTests, TriangulationStyleRequestProducesTriangles) {
    next::triangulation_request64 request;
    request.paths = {{
        {0, 0},
        {100, 0},
        {100, 100},
        {0, 100},
    }};
    request.use_delaunay = true;

    const auto result = next::triangulate(request);

    EXPECT_EQ(result.status, next::TriangulateResult::success);
    EXPECT_FALSE(result.triangles.empty());
}
