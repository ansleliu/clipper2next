#include "clipper2next/batch.h"
#include "clipper2next/clip.h"
#include "clipper2next/geometry/scaling.h"
#include "clipper2next/minkowski.h"
#include "clipper2next/offset.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/triangulation.h"
#include "support/test_paths.h"
#include "api/private/checked_execution.h"
#include "clip/private/boolean_union_service.h"

#include <expected>
#include <gtest/gtest.h>

#include <limits>
#include <new>
#include <stdexcept>
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
    const auto tree_result = next::clip_tree_checked(request);

    ASSERT_FALSE(clip_result.has_value());
    EXPECT_EQ(clip_result.error(), next::clipper_error_code::coordinate_range);
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
    const auto tree = next::clip_tree_checked(tree_request);

    ASSERT_FALSE(direct.has_value());
    EXPECT_EQ(direct.error(), next::clipper_error_code::coordinate_range);
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

TEST(Clipper2NextCheckedRequestApiTests, InvalidClipParametersPrecedeEmptyAndFastPaths) {
    for (const auto operation : {next::ClipType::Union, next::ClipType::NoClip,
                                  next::ClipType::Intersection}) {
        for (const auto empty : {false, true}) {
            for (int invalid = 0; invalid != 3; ++invalid) {
                SCOPED_TRACE(::testing::Message() << static_cast<int>(operation)
                                                << '/' << empty << '/' << invalid);
                next::clip_request64 request;
                request.clip_type = operation;
                if (!empty) {
                    request.subjects = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
                    request.clips = {{{25, 25}, {75, 25}, {75, 75}, {25, 75}}};
                }
                if (invalid == 0) { request.clip_type = static_cast<next::ClipType>(99); }
                if (invalid == 1) { request.fill_rule = static_cast<next::FillRule>(99); }
                if (invalid == 2) {
                    request.options.intersection_policy.mode = static_cast<next::precision_mode>(99);
                }
                const auto flat = next::clip_checked(request);
                const auto tree = next::clip_tree_checked(request);
                ASSERT_FALSE(flat);
                EXPECT_EQ(flat.error(), next::clipper_error_code::invalid_argument);
                ASSERT_FALSE(tree);
                EXPECT_EQ(tree.error(), next::clipper_error_code::invalid_argument);
            }
        }
    }
}

TEST(Clipper2NextCheckedRequestApiTests, AllFillRulesAcceptBooleanSelfIntersections) {
    for (const auto fill : {next::FillRule::EvenOdd, next::FillRule::NonZero,
                           next::FillRule::Positive, next::FillRule::Negative}) {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Union;
        request.fill_rule = fill;
        request.subjects = {{{0, 0}, {100, 100}, {100, 0}, {0, 100}}};
        EXPECT_TRUE(next::clip_checked(request));
        EXPECT_TRUE(next::clip_tree_checked(request));
    }
}

TEST(Clipper2NextCheckedRequestApiTests, ParametersWinWhenCoordinatesAreAlsoInvalid) {
    next::clip_request64 request;
    request.fill_rule = static_cast<next::FillRule>(99);
    request.subjects = {{{next::MAX_COORD + 1, 0}}};
    const auto flat = next::clip_checked(request);
    const auto tree = next::clip_tree_checked(request);
    ASSERT_FALSE(flat);
    EXPECT_EQ(flat.error(), next::clipper_error_code::invalid_argument);
    ASSERT_FALSE(tree);
    EXPECT_EQ(tree.error(), next::clipper_error_code::invalid_argument);
}

TEST(Clipper2NextCheckedRequestApiTests, EmptyRectanglesStillValidateEveryProvidedPoint) {
    for (const auto& path : {next::Path64{{next::MAX_COORD + 1, 0}},
                           next::Path64{{0, 0}, {next::MAX_COORD + 1, 0}}}) {
        next::rect_clip_request64 polygon;
        polygon.rect = {0, 0, 0, 0};
        polygon.paths = {path};
        const auto clipped = next::rect_clip_checked(polygon);
        ASSERT_FALSE(clipped);
        EXPECT_EQ(clipped.error(), next::clipper_error_code::coordinate_range);
        next::rect_clip_lines_request64 lines;
        lines.rect = polygon.rect;
        lines.lines = polygon.paths;
        const auto clipped_lines = next::rect_clip_lines_checked(lines);
        ASSERT_FALSE(clipped_lines);
        EXPECT_EQ(clipped_lines.error(), next::clipper_error_code::coordinate_range);
    }
    EXPECT_TRUE(next::rect_clip_checked(next::rect_clip_request64{}));
    EXPECT_TRUE(next::rect_clip_lines_checked(next::rect_clip_lines_request64{}));
}

TEST(Clipper2NextCheckedRequestApiTests, DoubleEntriesRejectNonFiniteIgnoredCoordinates) {
    for (const auto invalid : {(std::numeric_limits<double>::quiet_NaN)(),
                               (std::numeric_limits<double>::infinity)(),
                               -(std::numeric_limits<double>::infinity)()}) {
        next::triangulation_requestd triangles;
        triangles.paths = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{invalid, 5}}};
        const auto triangulated = next::triangulate_checked(triangles);
        ASSERT_FALSE(triangulated);
        EXPECT_EQ(triangulated.error(), next::clipper_error_code::coordinate_range);
        next::minkowski_requestd minkowski;
        minkowski.pattern = {{0, 0}, {invalid, 0}, {1, 1}};
        minkowski.path = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        const auto sum = next::minkowski_sum_checked(minkowski);
        const auto difference = next::minkowski_difference_checked(minkowski);
        ASSERT_FALSE(sum);
        EXPECT_EQ(sum.error(), next::clipper_error_code::coordinate_range);
        ASSERT_FALSE(difference);
        EXPECT_EQ(difference.error(), next::clipper_error_code::coordinate_range);
    }
}

TEST(Clipper2NextCheckedRequestApiTests, CheckedEnvelopeClassifiesPropagatingExceptions) {
    // Explicit exceptions also exercise normal Debug's iterator-2 ABI without
    // injecting inside the vendor's noexcept proxy constructors.
    const auto allocation =
        next::internal::checked_call<int>([]() -> int { throw std::bad_alloc{}; });
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error(), next::clipper_error_code::allocation_failure);
    const auto capacity =
        next::internal::checked_call<int>([]() -> int { throw std::length_error{"capacity"}; });
    ASSERT_FALSE(capacity);
    EXPECT_EQ(capacity.error(), next::clipper_error_code::resource_limit);
    const auto unknown = next::internal::checked_call<int>([]() -> int { throw 42; });
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error(), next::clipper_error_code::internal_error);
    for (auto code : {next::clipper_error_code::input_access_failure,
                      next::clipper_error_code::input_changed,
                      next::clipper_error_code::sink_failure,
                      next::clipper_error_code::executor_failure,
                      next::clipper_error_code::invalid_argument}) {
        const auto typed =
            next::internal::checked_call<int>([&]() -> int { next::raise_clipper_error(code); });
        ASSERT_FALSE(typed);
        EXPECT_EQ(typed.error(), code);
        const auto classified =
            next::internal::checked_call<int>([&] { return next::make_clipper_error<int>(code); });
        ASSERT_FALSE(classified);
        EXPECT_EQ(classified.error(), code);
    }
}

TEST(Clipper2NextCheckedRequestApiTests, UnionRejectsCoordinatesBeforeSingletonGeometry) {
    // A simple singleton would bypass the engine's range admission. Full int64
    // coordinates must never reach the narrower collinearity/simplicity tests.
    const auto low = (std::numeric_limits<int64_t>::min)();
    const auto high = (std::numeric_limits<int64_t>::max)();
    for (const next::Paths64& paths :
         {next::Paths64{
              {{next::MAX_COORD + 1, 0}, {next::MAX_COORD + 10, 0}, {next::MAX_COORD + 1, 10}}},
          next::Paths64{{{low, low}, {high, low}, {high, high}, {low, high}}},
          next::Paths64{{{next::MAX_COORD + 1, 0}}}}) {
        const auto result = next::internal::checked_call<next::paths64_result>(
            [&] { return next::internal::union_paths(paths); });
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), next::clipper_error_code::coordinate_range);
    }
}

TEST(Clipper2NextCheckedRequestApiTests, TriangulationKeepsEmptyAndCrossingDomainStatuses) {
    for (bool delaunay : {false, true}) {
        next::triangulation_request64 request;
        request.use_delaunay = delaunay;
        auto empty = next::triangulate_checked(request);
        ASSERT_TRUE(empty);
        EXPECT_EQ(empty->status, next::TriangulateResult::no_polygons);
        EXPECT_TRUE(empty->triangles.empty());
        request.paths = {{{0, 0}, {10, 10}, {0, 10}, {10, 0}}};
        const auto crossing = next::triangulate_checked(request);
        ASSERT_TRUE(crossing);
        EXPECT_EQ(crossing->status, next::TriangulateResult::paths_intersect);
        EXPECT_TRUE(crossing->triangles.empty());
        next::triangulation_requestd floating;
        floating.use_delaunay = delaunay;
        floating.paths = {{{0, 0}, {10, 10}, {0, 10}, {10, 0}}};
        const auto crossing_d = next::triangulate_checked(floating);
        ASSERT_TRUE(crossing_d);
        EXPECT_EQ(crossing_d->status, next::TriangulateResult::paths_intersect);
        EXPECT_TRUE(crossing_d->triangles.empty());
    }
}
