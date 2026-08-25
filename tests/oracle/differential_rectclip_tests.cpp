#include <gtest/gtest.h>

#include "path_equivalence.h"
#include "support/test_paths.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

#include <string>
#include <vector>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;
namespace test = clipper2next::tests;

namespace {

[[nodiscard]] auto execute_next_rect_clip(const next::Rect64& rect, const next::Paths64& paths)
    -> next::Paths64 {
    next::rect_clip_request64 request;
    request.rect = rect;
    request.paths = paths;
    return next::rect_clip(request).paths;
}

[[nodiscard]] auto execute_next_rect_clip_lines(const next::Rect64& rect,
                                                const next::Paths64& lines) -> next::Paths64 {
    next::rect_clip_lines_request64 request;
    request.rect = rect;
    request.lines = lines;
    return next::rect_clip_lines(request).paths;
}

[[nodiscard]] auto generated_rectclip_subject(std::size_t index) -> next::Path64 {
    const auto base_x = static_cast<int64_t>((index % 9U) * 35U) - 120;
    const auto base_y = static_cast<int64_t>((index / 9U) * 27U) - 80;
    const auto width = static_cast<int64_t>(95U + (index % 5U) * 17U);
    const auto height = static_cast<int64_t>(80U + (index % 7U) * 13U);
    const auto notch = static_cast<int64_t>(10U + (index % 4U) * 6U);
    return next::Path64{{base_x, base_y},
                        {base_x + width, base_y},
                        {base_x + width, base_y + height / 2},
                        {base_x + width - notch, base_y + height / 2},
                        {base_x + width - notch, base_y + height},
                        {base_x, base_y + height}};
}

}  // namespace

TEST(Clipper2NextDifferentialRectClipTests, PolygonClippedToRectMatchesLegacy) {
    const auto legacy_rect = legacy::Rect64{25, 25, 125, 125};
    const auto next_rect = next::Rect64{25, 25, 125, 125};
    const auto legacy_subject = legacy::Paths64{
        legacy::MakePath({0, 0, 150, 0, 150, 150, 0, 150}),
    };
    const auto next_subject = next::Paths64{
        {{0, 0}, {150, 0}, {150, 150}, {0, 150}},
    };

    const auto expected = legacy::RectClip(legacy_rect, legacy_subject);
    const auto actual = execute_next_rect_clip(next_rect, next_subject);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialRectClipTests, RectClipLinesMatchesLegacy) {
    const auto rect = next::Rect64{-10, -10, 110, 110};
    const auto lines = next::Paths64{
        test::path64({-50, 50, 25, 50, 150, 50}),
        test::path64({30, -80, 30, 30, 30, 180}),
        test::path64({-40, -40, 140, 140}),
    };

    const auto expected = legacy::RectClipLines(
        legacy::Rect64{rect.left, rect.top, rect.right, rect.bottom},
        oracle::to_legacy_paths(lines));
    const auto actual = execute_next_rect_clip_lines(rect, lines);

    EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(expected, actual));
}

TEST(Clipper2NextDifferentialRectClipTests, RectClipLinesContainedBoundaryMatchesLegacy) {
    const auto rect = next::Rect64{0, 0, 100, 100};
    const auto lines = next::Paths64{
        test::path64({0, 0, 50, 50, 100, 100}),
        test::path64({0, 100, 25, 75, 100, 0}),
        test::path64({10, 90, 90, 10}),
    };

    const auto expected = legacy::RectClipLines(
        legacy::Rect64{rect.left, rect.top, rect.right, rect.bottom},
        oracle::to_legacy_paths(lines));
    const auto actual = execute_next_rect_clip_lines(rect, lines);

    EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(expected, actual));
}

TEST(Clipper2NextDifferentialRectClipTests, GeneratedPolygonCorpusMatchesLegacy) {
    const std::vector<next::Rect64> rects{
        {0, 0, 100, 100}, {25, -20, 135, 120}, {-60, 30, 80, 160}, {40, 40, 160, 160}};
    const std::vector<next::Paths64> subjects{
        {test::path64({-50, -20, 150, -20, 150, 40, 40, 40, 40, 150, -50, 150})},
        {test::path64({20, -40, 140, 20, 100, 140, -20, 80})},
        {
            test::path64({-20, -20, 70, -20, 70, 70, -20, 70}),
            test::path64({90, 10, 170, 10, 170, 90, 90, 90}),
        },
        {test::path64({0, 0, 50, 0, 50, 50, 0, 50})}};

    for (std::size_t rect_index = 0; rect_index < rects.size(); ++rect_index) {
        for (std::size_t subject_index = 0; subject_index < subjects.size(); ++subject_index) {
            SCOPED_TRACE("rect=" + std::to_string(rect_index) +
                         " subject=" + std::to_string(subject_index));
            const auto expected =
                legacy::RectClip(legacy::Rect64{rects[rect_index].left,
                                                rects[rect_index].top,
                                                rects[rect_index].right,
                                                rects[rect_index].bottom},
                                 oracle::to_legacy_paths(subjects[subject_index]));
            const auto actual = execute_next_rect_clip(rects[rect_index], subjects[subject_index]);

            EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
        }
    }
}

TEST(Clipper2NextDifferentialRectClipTests, DeterministicFuzzPolygonCorpusMatchesLegacy) {
    const std::vector<next::Rect64> rects{
        {-40, -20, 130, 120}, {0, 0, 180, 160}, {50, -60, 210, 100}, {-120, 30, 80, 210}};

    for (std::size_t index = 0; index < 96U; ++index) {
        const auto subjects = next::Paths64{generated_rectclip_subject(index)};
        for (std::size_t rect_index = 0; rect_index < rects.size(); ++rect_index) {
            SCOPED_TRACE("case=" + std::to_string(index) + " rect=" + std::to_string(rect_index));
            const auto expected = legacy::RectClip(legacy::Rect64{rects[rect_index].left,
                                                                  rects[rect_index].top,
                                                                  rects[rect_index].right,
                                                                  rects[rect_index].bottom},
                                                   oracle::to_legacy_paths(subjects));
            const auto actual = execute_next_rect_clip(rects[rect_index], subjects);
            EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
        }
    }
}
