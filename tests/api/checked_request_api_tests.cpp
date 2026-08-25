#include "clipper2next/batch.h"
#include "clipper2next/clip.h"
#include "clipper2next/geometry/scaling.h"
#include "clipper2next/minkowski.h"
#include "clipper2next/offset.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/triangulation.h"
#include "support/test_paths.h"

#include <expected>
#include <gtest/gtest.h>

#include <limits>
#include <type_traits>
#include <vector>

namespace next = clipper2next;
namespace test = clipper2next::tests;

namespace {

[[nodiscard]] auto out_of_range_rectangle_clip_request() -> next::clip_request64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({next::MAX_COORD + 1,
                      0,
                      next::MAX_COORD + 101,
                      0,
                      next::MAX_COORD + 101,
                      100,
                      next::MAX_COORD + 1,
                      100}),
    };
    request.clips = next::Paths64{
        test::path64({next::MAX_COORD + 25,
                      25,
                      next::MAX_COORD + 75,
                      25,
                      next::MAX_COORD + 75,
                      75,
                      next::MAX_COORD + 25,
                      75}),
    };
    return request;
}

[[nodiscard]] auto out_of_range_nested_rectangle_clip_tree_request() -> next::clip_request64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({next::MAX_COORD + 1,
                      0,
                      next::MAX_COORD + 101,
                      0,
                      next::MAX_COORD + 101,
                      100,
                      next::MAX_COORD + 1,
                      100}),
        test::path64({next::MAX_COORD + 25,
                      25,
                      next::MAX_COORD + 75,
                      25,
                      next::MAX_COORD + 75,
                      75,
                      next::MAX_COORD + 25,
                      75}),
    };
    return request;
}

}  // namespace

static_assert(std::is_same_v<next::expected_paths64_result,
                             std::expected<next::paths64_result,
                                           next::clipper_error_code>>);

TEST(Clipper2NextCheckedRequestApiTests, CheckedClipApisReturnCoordinateRangeForOutOfRangeInput) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({next::MAX_COORD + 1, 0, next::MAX_COORD + 2, 0, next::MAX_COORD + 2, 10}),
    };

    const auto clip_result = next::clip_checked(request);
    const auto prepared_result = next::clip_checked(next::prepare_clip_request(request));
    const auto tree_result = next::clip_tree_checked(request);

    ASSERT_FALSE(clip_result.has_value());
    EXPECT_EQ(clip_result.error(), next::clipper_error_code::coordinate_range);
    ASSERT_FALSE(prepared_result.has_value());
    EXPECT_EQ(prepared_result.error(), next::clipper_error_code::coordinate_range);
    ASSERT_FALSE(tree_result.has_value());
    EXPECT_EQ(tree_result.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedClipWrappersExposeErgonomicPublicApi) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto direct = next::clip_checked(request);
    const auto prepared = next::clip_checked(next::prepare_clip_request(request));
    const auto tree = next::clip_tree_checked(request);

    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(prepared.has_value());
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(direct.value().closed.size(), 1U);
    EXPECT_EQ(prepared.value().closed, direct.value().closed);
    EXPECT_EQ(tree.value().tree.count(), 1U);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedClipWrappersReportCoordinateRange) {
    const auto request = out_of_range_rectangle_clip_request();
    const auto tree_request = out_of_range_nested_rectangle_clip_tree_request();

    const auto direct = next::clip_checked(request);
    const auto prepared = next::clip_checked(next::prepare_clip_request(request));
    const auto tree = next::clip_tree_checked(tree_request);

    ASSERT_FALSE(direct.has_value());
    EXPECT_EQ(direct.error(), next::clipper_error_code::coordinate_range);
    ASSERT_FALSE(prepared.has_value());
    EXPECT_EQ(prepared.error(), next::clipper_error_code::coordinate_range);
    ASSERT_FALSE(tree.has_value());
    EXPECT_EQ(tree.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedOffsetAndRectClipApisReturnCoordinateRange) {
    next::offset_request64 offset_request;
    offset_request.paths = next::Paths64{
        test::path64({next::MAX_COORD + 1, 0, next::MAX_COORD + 2, 0, next::MAX_COORD + 2, 10}),
    };

    const auto offset_result = next::offset_checked(offset_request);

    ASSERT_FALSE(offset_result.has_value());
    EXPECT_EQ(offset_result.error(), next::clipper_error_code::coordinate_range);

    next::rect_clip_request64 rect_request;
    rect_request.rect = next::Rect64{0, 0, 100, 100};
    rect_request.paths = offset_request.paths;
    const auto rect_path_result = next::rect_clip_checked(rect_request);

    ASSERT_FALSE(rect_path_result.has_value());
    EXPECT_EQ(rect_path_result.error(), next::clipper_error_code::coordinate_range);

    next::rect_clip_lines_request64 line_request;
    line_request.rect = next::Rect64{next::MIN_COORD - 1, 0, 100, 100};
    line_request.lines = next::Paths64{{{0, 0}, {10, 10}}};
    const auto line_result = next::rect_clip_lines_checked(line_request);

    ASSERT_FALSE(line_result.has_value());
    EXPECT_EQ(line_result.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedMinkowskiApisReturnCoordinateRange) {
    next::minkowski_request64 request;
    request.pattern = test::path64({next::MAX_COORD + 1, 0, next::MAX_COORD + 2, 0});
    request.path = test::path64({0, 0, 10, 0});

    const auto sum = next::minkowski_sum_checked(request);
    const auto difference = next::minkowski_difference_checked(request);

    ASSERT_FALSE(sum.has_value());
    EXPECT_EQ(sum.error(), next::clipper_error_code::coordinate_range);
    ASSERT_FALSE(difference.has_value());
    EXPECT_EQ(difference.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedMinkowskiApisRejectOutOfRangeTranslations) {
    next::minkowski_request64 sum_request;
    sum_request.pattern =
        next::Path64{{next::MAX_COORD, 0}, {next::MAX_COORD, 1}};
    sum_request.path = next::Path64{{1, 0}, {1, 1}};

    const auto sum = next::minkowski_sum_checked(sum_request);

    ASSERT_FALSE(sum.has_value());
    EXPECT_EQ(sum.error(), next::clipper_error_code::coordinate_range);

    next::minkowski_request64 difference_request;
    difference_request.pattern =
        next::Path64{{next::MIN_COORD, 0}, {next::MIN_COORD, 1}};
    difference_request.path = next::Path64{{1, 0}, {1, 1}};

    const auto difference = next::minkowski_difference_checked(difference_request);

    ASSERT_FALSE(difference.has_value());
    EXPECT_EQ(difference.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedMinkowskiDoubleApisReturnPrecisionErrors) {
    next::minkowski_requestd request;
    request.pattern = next::PathD{{0, 0}, {10, 0}};
    request.path = next::PathD{{0, 0}, {10, 0}};
    request.decimal_precision = 30;

    const auto sum = next::minkowski_sum_checked(request);
    const auto difference = next::minkowski_difference_checked(request);

    ASSERT_FALSE(sum.has_value());
    EXPECT_EQ(sum.error(), next::clipper_error_code::precision_out_of_range);
    ASSERT_FALSE(difference.has_value());
    EXPECT_EQ(difference.error(), next::clipper_error_code::precision_out_of_range);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedTriangulationApisReturnCoordinateRange) {
    next::triangulation_request64 request;
    request.paths = next::Paths64{
        test::path64({next::MAX_COORD + 1, 0, next::MAX_COORD + 2, 0, next::MAX_COORD + 2, 10}),
    };

    const auto result = next::triangulate_checked(request);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextCheckedRequestApiTests, UncheckedTriangulationRejectsOutOfRangeInput) {
    next::triangulation_request64 request;
    request.paths = next::Paths64{{
        {(std::numeric_limits<int64_t>::min)(), (std::numeric_limits<int64_t>::min)()},
        {(std::numeric_limits<int64_t>::max)(), (std::numeric_limits<int64_t>::min)()},
        {(std::numeric_limits<int64_t>::max)(), (std::numeric_limits<int64_t>::max)()},
    }};

    const auto result = next::triangulate(request);

    EXPECT_EQ(result.status, next::TriangulateResult::fail);
    EXPECT_TRUE(result.triangles.empty());
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedTriangulationDoubleApiReturnsPrecisionError) {
    next::triangulation_requestd request;
    request.paths = next::PathsD{{{0, 0}, {10, 0}, {0, 10}}};
    request.decimal_precision = 30;

    const auto result = next::triangulate_checked(request);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::precision_out_of_range);
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedMinkowskiAndTriangulationApisReturnValues) {
    next::minkowski_request64 minkowski_request;
    minkowski_request.pattern = test::path64({0, 0, 10, 0, 10, 10, 0, 10});
    minkowski_request.path = test::path64({0, 0, 20, 0, 20, 20, 0, 20});

    const auto sum = next::minkowski_sum_checked(minkowski_request);
    ASSERT_TRUE(sum.has_value());
    EXPECT_FALSE(sum.value().empty());

    next::triangulation_request64 triangulation_request;
    triangulation_request.paths = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto triangulation = next::triangulate_checked(triangulation_request);
    ASSERT_TRUE(triangulation.has_value());
    EXPECT_EQ(triangulation.value().status, next::TriangulateResult::success);
    EXPECT_FALSE(triangulation.value().triangles.empty());
}
