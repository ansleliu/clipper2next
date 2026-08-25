#include <gtest/gtest.h>

#include "clipper2next/clipper.h"
#include "clipper2next/geometry.h"

#include <algorithm>
#include <cmath>

namespace next = clipper2next;

namespace {

auto rectangle(int64_t left, int64_t top, int64_t right, int64_t bottom) -> next::Path64 {
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

auto absolute_area(const next::Paths64& paths) -> double {
    return std::fabs(next::area(paths));
}

auto union_paths(const next::Paths64& subjects, const next::Paths64& clips = {}) -> next::Paths64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = subjects;
    request.clips = clips;
    return next::clip(request).closed;
}

auto offset_paths(const next::Paths64& paths) -> next::Paths64 {
    next::offset_request64 request;
    request.paths = paths;
    request.delta = 12.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;
    return next::offset(request).closed;
}

auto rect_clip_lines(const next::Rect64& rect, const next::Paths64& lines) -> next::Paths64 {
    next::rect_clip_lines_request64 request;
    request.rect = rect;
    request.lines = lines;
    return next::rect_clip_lines(request).paths;
}

auto rect_to_path(const next::Rect64& rect) -> next::Path64 {
    return {{rect.left, rect.top},
            {rect.right, rect.top},
            {rect.right, rect.bottom},
            {rect.left, rect.bottom}};
}

auto open_clip_lines(const next::Rect64& rect, const next::Paths64& lines) -> next::Paths64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.open_subjects = lines;
    request.clips = next::Paths64{rect_to_path(rect)};
    return next::clip(request).open;
}

auto generated_convex_polygon(int index) -> next::Path64 {
    const auto x = static_cast<int64_t>(index * 250);
    const auto y = static_cast<int64_t>((index % 5) * 180);
    const auto side = static_cast<int64_t>(80 + ((index * 13) % 70));
    return {{x, y + side / 3},
            {x + side / 4, y},
            {x + side, y},
            {x + side + side / 3, y + side / 2},
            {x + side, y + side},
            {x + side / 5, y + side + side / 4}};
}

auto generated_notched_polygon(int index) -> next::Path64 {
    const auto x = static_cast<int64_t>(index * 320);
    const auto y = static_cast<int64_t>((index % 7) * 240);
    const auto width = static_cast<int64_t>(180 + (index % 5) * 20);
    const auto height = static_cast<int64_t>(150 + (index % 4) * 18);
    const auto notch = static_cast<int64_t>(40 + (index % 6) * 7);
    return {{x, y},
            {x + width, y},
            {x + width, y + notch},
            {x + width / 2, y + notch},
            {x + width / 2, y + height - notch},
            {x + width, y + height - notch},
            {x + width, y + height},
            {x, y + height}};
}

auto generated_line_family(int index) -> next::Paths64 {
    const auto x = static_cast<int64_t>((index % 24) * 20 - 240);
    const auto y = static_cast<int64_t>((index % 18) * 20 - 180);
    const auto shift = static_cast<int64_t>((index % 9) * 20 - 80);
    return {
        {{-260, y}, {240, y}},
        {{x, -220}, {x, 220}},
        {{-260 + shift, -260}, {260 + shift, 260}},
        {{-260 + shift, 260}, {260 + shift, -260}},
    };
}

}  // namespace

TEST(Clipper2NextPropertyTests, BooleanUnionIsTranslationInvariant) {
    const next::Paths64 subjects{rectangle(0, 0, 100, 100)};
    const next::Paths64 clips{rectangle(40, 10, 140, 110)};

    const auto result = union_paths(subjects, clips);
    const auto translated_result =
        union_paths(next::translate(subjects, 200, -75), next::translate(clips, 200, -75));

    EXPECT_NEAR(absolute_area(translated_result), absolute_area(result), 0.001);
    EXPECT_EQ(next::bounds(translated_result), next::Rect64(200, -75, 340, 35));
}

TEST(Clipper2NextPropertyTests, BooleanUnionIsOrientationInvariantForClosedInputs) {
    next::Paths64 subjects{rectangle(0, 0, 100, 100)};
    next::Paths64 clips{rectangle(50, 50, 150, 150)};
    auto reversed_subjects = subjects;
    auto reversed_clips = clips;
    std::reverse(reversed_subjects.front().begin(), reversed_subjects.front().end());
    std::reverse(reversed_clips.front().begin(), reversed_clips.front().end());

    const auto result = union_paths(subjects, clips);
    const auto reversed_result = union_paths(reversed_subjects, reversed_clips);

    EXPECT_NEAR(absolute_area(reversed_result), absolute_area(result), 0.001);
    EXPECT_EQ(next::bounds(reversed_result), next::bounds(result));
}

TEST(Clipper2NextPropertyTests, PathDScaleRoundtripPreservesPinnedArea) {
    const next::PathsD subjects{
        next::PathD{{0.25, 0.25}, {10.25, 0.25}, {10.25, 10.25}, {0.25, 10.25}},
    };

    const auto scaled = next::scale_paths<int64_t>(subjects, next::scale_request{100.0, 100.0});
    ASSERT_TRUE(scaled.has_value());

    const auto result = next::scale_paths<double>(scaled.value(), next::scale_request{0.01, 0.01});
    ASSERT_TRUE(result.has_value());

    ASSERT_EQ(result.value().size(), 1U);
    EXPECT_NEAR(std::fabs(next::area(result.value())), 100.0, 0.001);
    EXPECT_NEAR(next::bounds(result.value()).left, 0.25, 0.001);
    EXPECT_NEAR(next::bounds(result.value()).right, 10.25, 0.001);
}

TEST(Clipper2NextPropertyTests, RepeatedClipAndOffsetExecutionIsDeterministic) {
    const next::Paths64 subjects{rectangle(0, 0, 100, 100)};
    const next::Paths64 clips{rectangle(25, 25, 125, 125)};

    const auto expected_union = union_paths(subjects, clips);
    const auto expected_offset = offset_paths(subjects);

    for (int iteration = 0; iteration < 20; ++iteration) {
        EXPECT_EQ(union_paths(subjects, clips), expected_union) << iteration;
        EXPECT_EQ(offset_paths(subjects), expected_offset) << iteration;
    }
}

TEST(Clipper2NextPropertyTests, GeneratedNonConvexTriangulationPreservesArea) {
    for (int index = 0; index < 32; ++index) {
        const next::Paths64 polygon{generated_notched_polygon(index)};
        next::triangulation_request64 request;
        request.paths = polygon;
        request.use_delaunay = (index % 2) == 0;

        const auto result = next::triangulate(request);

        SCOPED_TRACE(index);
        ASSERT_EQ(result.status, next::TriangulateResult::success);
        ASSERT_FALSE(result.triangles.empty());
        for (const auto& triangle : result.triangles) {
            EXPECT_EQ(triangle.size(), 3U);
            EXPECT_GT(std::fabs(next::area(triangle)), 0.0);
        }
        EXPECT_NEAR(absolute_area(result.triangles), absolute_area(polygon), 0.001);
    }
}

TEST(Clipper2NextPropertyTests, GeneratedHoleTriangulationPreservesNetArea) {
    for (int index = 0; index < 16; ++index) {
        const auto shift = static_cast<int64_t>(index * 250);
        auto outer = rectangle(shift, 0, shift + 180, 160);
        auto hole = rectangle(shift + 45, 40, shift + 125, 115);
        std::reverse(hole.begin(), hole.end());
        const next::Paths64 polygon{outer, hole};

        next::triangulation_request64 request;
        request.paths = polygon;
        request.use_delaunay = (index % 2) != 0;

        const auto result = next::triangulate(request);

        SCOPED_TRACE(index);
        ASSERT_EQ(result.status, next::TriangulateResult::success);
        ASSERT_FALSE(result.triangles.empty());
        const auto expected_area = std::fabs(next::area(outer)) - std::fabs(next::area(hole));
        EXPECT_NEAR(absolute_area(result.triangles), expected_area, 0.001);
    }
}

TEST(Clipper2NextPropertyTests, PositiveConvexOffsetIsAreaMonotonic) {
    const next::Paths64 subject{rectangle(0, 0, 100, 100)};
    next::offset_request64 request;
    request.paths = subject;
    request.delta = 10.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;
    const auto offset = next::offset(request).closed;

    EXPECT_GT(absolute_area(offset), absolute_area(subject));
    EXPECT_TRUE(next::bounds(offset).contains(next::bounds(subject)));
}

TEST(Clipper2NextPropertyTests, RectClipLinesIsTranslationInvariant) {
    const next::Rect64 rect{0, 0, 100, 100};
    const next::Paths64 lines{{{-30, 20}, {30, 20}, {130, 80}},
                              {{50, -40}, {50, 20}, {70, 130}},
                              {{-20, -20}, {120, 120}}};

    const auto expected = rect_clip_lines(rect, lines);
    const auto translated =
        rect_clip_lines(next::Rect64{200, -75, 300, 25}, next::translate(lines, 200, -75));

    EXPECT_EQ(next::translate(translated, -200, 75), expected);
}

TEST(Clipper2NextPropertyTests, OpenLineClipIsTranslationInvariant) {
    const next::Rect64 rect{0, 0, 100, 100};
    const next::Paths64 lines{{{-20, 25}, {25, 25}, {140, 25}},
                              {{10, -30}, {10, 10}, {90, 90}, {130, 90}},
                              {{20, 120}, {120, 20}}};

    const auto expected = open_clip_lines(rect, lines);
    const auto translated =
        open_clip_lines(next::Rect64{-300, 500, -200, 600}, next::translate(lines, -300, 500));

    EXPECT_EQ(next::translate(translated, 300, -500), expected);
}

TEST(Clipper2NextPropertyTests, GeneratedLineClipFamiliesAreTranslationInvariant) {
    for (int index = 0; index < 64; ++index) {
        SCOPED_TRACE(index);
        const auto rect = next::Rect64{-120, -90, 180, 150};
        const auto lines = generated_line_family(index);
        const auto dx = static_cast<int64_t>(index * 13 - 320);
        const auto dy = static_cast<int64_t>(250 - index * 9);

        const auto expected_rectclip = rect_clip_lines(rect, lines);
        const auto expected_open_clip = open_clip_lines(rect, lines);
        const auto translated_rect = next::Rect64{
            rect.left + dx,
            rect.top + dy,
            rect.right + dx,
            rect.bottom + dy,
        };
        const auto translated_lines = next::translate(lines, dx, dy);

        EXPECT_EQ(next::translate(rect_clip_lines(translated_rect, translated_lines), -dx, -dy),
                  expected_rectclip);
        EXPECT_EQ(next::translate(open_clip_lines(translated_rect, translated_lines), -dx, -dy),
                  expected_open_clip);
    }
}

TEST(Clipper2NextPropertyTests, GeneratedConvexTriangulationPreservesArea) {
    for (int index = 0; index < 32; ++index) {
        const next::Paths64 polygon{generated_convex_polygon(index)};
        next::triangulation_request64 request;
        request.paths = polygon;
        request.use_delaunay = true;

        const auto result = next::triangulate(request);

        SCOPED_TRACE(index);
        ASSERT_EQ(result.status, next::TriangulateResult::success);
        ASSERT_FALSE(result.triangles.empty());
        for (const auto& triangle : result.triangles) {
            EXPECT_EQ(triangle.size(), 3U);
            EXPECT_GT(std::fabs(next::area(triangle)), 0.0);
        }
        EXPECT_NEAR(absolute_area(result.triangles), absolute_area(polygon), 0.001);
    }
}
