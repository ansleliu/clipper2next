#include <gtest/gtest.h>

#include "clipper2next/clip.h"
#include "clipper2next/clipper.h"
#include "clipper2next/geometry.h"
#include "clipper2next/offset/builder.h"
#include "clipper2next/polygon/poly_tree.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/triangulation.h"

#include <cmath>

namespace next = clipper2next;

namespace {

auto square_path(int64_t left, int64_t top, int64_t right, int64_t bottom) -> next::Path64 {
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

auto absolute_area(const next::Paths64& paths) -> double {
    return std::fabs(next::area(paths));
}

auto clip_area(next::ClipType clip_type, const next::Paths64& subjects, const next::Paths64& clips)
    -> double {
    next::clip_request64 request;
    request.clip_type = clip_type;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = subjects;
    request.clips = clips;
    return absolute_area(next::clip(request).closed);
}

}  // namespace

TEST(Clipper2NextGoldenFixturesTests, BooleanOperationsMatchPinnedAreas) {
    const next::Paths64 subjects{square_path(0, 0, 100, 100)};
    const next::Paths64 clips{square_path(50, 50, 150, 150)};

    EXPECT_NEAR(clip_area(next::ClipType::Union, subjects, clips), 17500.0, 0.001);
    EXPECT_NEAR(clip_area(next::ClipType::Intersection, subjects, clips), 2500.0, 0.001);
    EXPECT_NEAR(clip_area(next::ClipType::Difference, subjects, clips), 7500.0, 0.001);
    EXPECT_NEAR(clip_area(next::ClipType::Xor, subjects, clips), 15000.0, 0.001);
}

TEST(Clipper2NextGoldenFixturesTests, OffsetPolygonOpenPathAndPolyTreeHavePinnedGeometry) {
    const next::Paths64 square{square_path(0, 0, 100, 100)};
    const auto polygon_offset = next::offset_builder{}
                                    .delta(10.0)
                                    .join(next::JoinType::Miter)
                                    .end(next::EndType::Polygon)
                                    .add(square)
                                    .execute();

    ASSERT_EQ(polygon_offset.size(), 1U);
    EXPECT_EQ(next::bounds(polygon_offset), next::Rect64(-10, -10, 110, 110));
    EXPECT_NEAR(absolute_area(polygon_offset), 14400.0, 0.001);

    const next::Paths64 line{{{0, 0}, {100, 0}}};
    const auto open_offset = next::offset_builder{}
                                 .delta(10.0)
                                 .join(next::JoinType::Miter)
                                 .end(next::EndType::Butt)
                                 .add(line)
                                 .execute();

    ASSERT_EQ(open_offset.size(), 1U);
    EXPECT_EQ(next::bounds(open_offset), next::Rect64(0, -10, 100, 10));
    EXPECT_NEAR(absolute_area(open_offset), 2000.0, 0.001);

    next::PolyTree64 tree;
    next::offset_builder{}
        .delta(10.0)
        .join(next::JoinType::Miter)
        .end(next::EndType::Polygon)
        .add(square)
        .execute_into(tree);

    EXPECT_EQ(tree.count(), 1U);
    EXPECT_NEAR(std::fabs(tree.area()), 14400.0, 0.001);
}

TEST(Clipper2NextGoldenFixturesTests, RectClipPolygonAndLineHavePinnedBounds) {
    const next::Rect64 rect{0, 0, 100, 100};
    const next::Path64 oversized_square = square_path(-20, -30, 120, 130);
    const auto clipped =
        next::rect_clip(next::rect_clip_request64{rect, next::Paths64{oversized_square}}).paths;

    ASSERT_EQ(clipped.size(), 1U);
    EXPECT_EQ(next::bounds(clipped), rect);
    EXPECT_NEAR(absolute_area(clipped), 10000.0, 0.001);

    const next::Path64 line{{-20, 50}, {120, 50}};
    const auto clipped_line =
        next::rect_clip_lines(next::rect_clip_lines_request64{rect, next::Paths64{line}}).paths;

    ASSERT_EQ(clipped_line.size(), 1U);
    ASSERT_EQ(clipped_line.front().size(), 2U);
    EXPECT_EQ(next::bounds(clipped_line), next::Rect64(0, 50, 100, 50));
}

TEST(Clipper2NextGoldenFixturesTests, TriangulationSquareAndHoleFixturesPreserveArea) {
    const next::Paths64 square{square_path(0, 0, 100, 100)};
    const auto square_triangles = next::triangulate(next::triangulation_request64{square, true});

    EXPECT_EQ(square_triangles.status, next::TriangulateResult::success);
    EXPECT_NEAR(absolute_area(square_triangles.triangles), 10000.0, 0.001);

    const next::Paths64 with_hole{
        square_path(0, 0, 200, 200),
        next::Path64{{50, 50}, {50, 150}, {150, 150}, {150, 50}},
    };
    const auto hole_triangles = next::triangulate(next::triangulation_request64{with_hole, true});

    EXPECT_EQ(hole_triangles.status, next::TriangulateResult::success);
    EXPECT_NEAR(absolute_area(hole_triangles.triangles), 30000.0, 0.001);

    const next::Paths64 degenerate{{{0, 0}, {0, 0}, {0, 0}, {0, 0}}};
    const auto degenerate_triangles =
        next::triangulate(next::triangulation_request64{degenerate, true});
    EXPECT_NE(degenerate_triangles.status, next::TriangulateResult::success);
    EXPECT_TRUE(degenerate_triangles.triangles.empty());
}
