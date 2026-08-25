#include "clipper2next/api/error.h"
#include "clipper2next/api/memory.h"
#include "clipper2next/api/options.h"
#include "clipper2next/api/result.h"
#include "clipper2next/batch.h"
#include "clipper2next/clip.h"
#include "clipper2next/clip/request.h"
#include "clipper2next/clip/topology.h"
#include "clipper2next/clip/types.h"
#include "clipper2next/clipper.h"
#include "clipper2next/core.h"
#include "clipper2next/core/fill_rule.h"
#include "clipper2next/core/path.h"
#include "clipper2next/core/point.h"
#include "clipper2next/core/rect.h"
#include "clipper2next/geometry.h"
#include "clipper2next/geometry/algorithms.h"
#include "clipper2next/geometry/core.h"
#include "clipper2next/geometry/line_intersections.h"
#include "clipper2next/geometry/math.h"
#include "clipper2next/geometry/path_transforms.h"
#include "clipper2next/geometry/predicates.h"
#include "clipper2next/geometry/scale.h"
#include "clipper2next/geometry/scaling.h"
#include "clipper2next/geometry/translate.h"
#include "clipper2next/minkowski.h"
#include "clipper2next/minkowski/request.h"
#include "clipper2next/offset.h"
#include "clipper2next/offset/borrowed.h"
#include "clipper2next/offset/builder.h"
#include "clipper2next/offset/request.h"
#include "clipper2next/offset/types.h"
#include "clipper2next/polygon/poly_tree.h"
#include "clipper2next/polygon/tree.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/rectclip/request.h"
#include "clipper2next/triangulation.h"
#include "clipper2next/triangulation/request.h"
#include "clipper2next/version.h"

#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace next = clipper2next;

template <typename T>
concept has_deterministic_member = requires(T value) {
    value.deterministic;
};

template <typename T>
concept exposes_runtime_data = requires(const T& value) {
    value.runtime_data();
};

template <typename T>
concept exposes_path_bounds = requires(const T& value) {
    value.path_bounds();
};

static_assert(!has_deterministic_member<next::execution_options>);
static_assert(!exposes_runtime_data<next::prepared_clip_request64>);
static_assert(!std::is_aggregate_v<next::prepared_clip_request64>);
static_assert(!std::is_constructible_v<next::prepared_rect_clip_request64,
                                       next::rect_clip_request64,
                                       std::vector<next::Rect64>>);
static_assert(!std::is_aggregate_v<next::prepared_rect_clip_request64>);
static_assert(!std::is_aggregate_v<next::immutable_rect_clip_paths64>);
static_assert(!exposes_path_bounds<next::prepared_rect_clip_request64>);
static_assert(!exposes_runtime_data<next::immutable_rect_clip_paths64>);
static_assert(!std::is_aggregate_v<next::borrowed_paths64>);
static_assert(!std::is_aggregate_v<next::topology_writer64>);
static_assert(std::is_trivially_copyable_v<next::borrowed_paths64>);
static_assert(std::is_trivially_copyable_v<next::topology_writer64>);
static_assert(sizeof(next::borrowed_paths64) == 9U * sizeof(void*));
static_assert(sizeof(next::topology_writer64) == 5U * sizeof(void*));
static_assert(std::is_same_v<next::Path64, std::vector<next::Point64>>);
static_assert(std::is_same_v<next::Paths64, std::vector<next::Path64>>);

TEST(Clipper2NextPublicHeaderTests, UmbrellaHeadersExposeExpectedTypes) {
    next::Paths64 paths;
    next::execution_options options;
    next::paths64_result result;

    EXPECT_TRUE(paths.empty());
    EXPECT_FALSE(options.reverse_solution);
    EXPECT_TRUE(result.closed.empty());
}

TEST(Clipper2NextPublicHeaderTests, ThinCoreHeadersAreIndependentlyUsable) {
    const next::Point64 point{3, 4};
    const next::Path64 path{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const next::Rect64 rect{0, 0, 10, 10};

    EXPECT_EQ(point.x, 3);
    EXPECT_EQ(path.size(), 4U);
    EXPECT_EQ(path[1].x, 10);
    EXPECT_EQ(path[2].y, 10);
    EXPECT_EQ(rect.width(), 10);
    EXPECT_EQ(next::area(path), 100.0);
}

TEST(Clipper2NextPublicHeaderTests, PublicHeadersExposeStableFacadeTypes) {
    next::offset_builder offset_builder;
    next::clip_request64 clip_request;
    next::offset_request64 offset_request;
    next::rect_clip_request64 rect_clip_request;

    EXPECT_TRUE(next::clip(clip_request).closed.empty());
    EXPECT_TRUE(next::offset(offset_request).closed.empty());
    EXPECT_TRUE(next::rect_clip(rect_clip_request).paths.empty());
    EXPECT_TRUE(offset_builder.execute().empty());
}

TEST(Clipper2NextPublicHeaderTests, PreparedHandlesExposeReadOnlySnapshots) {
    EXPECT_TRUE((std::is_assignable_v<next::prepared_clip_request64&,
                                      next::prepared_clip_request64>));
    EXPECT_TRUE((std::is_assignable_v<next::prepared_rect_clip_request64&,
                                      next::prepared_rect_clip_request64>));
    EXPECT_FALSE((std::is_assignable_v<
                  decltype((std::declval<next::prepared_clip_request64&>().request())),
                  next::clip_request64>));
    EXPECT_FALSE((std::is_assignable_v<
                  decltype((std::declval<next::prepared_clip_request64&>().metadata())),
                  next::clip_request_metadata64>));
    EXPECT_FALSE((std::is_assignable_v<
                  decltype((std::declval<next::prepared_rect_clip_request64&>().request())),
                  next::rect_clip_request64>));
}

TEST(Clipper2NextPublicHeaderTests, RectClipFacadeHasNoInternalShape) {
    next::rect_clip_request64 request;
    request.rect = next::Rect64{0, 0, 10, 10};
    const auto result = next::rect_clip(request);

    EXPECT_TRUE(result.paths.empty());
}

TEST(Clipper2NextPublicHeaderTests, ScalarConvenienceDeclarationsLink) {
    const next::Path64 path{{0, 0}, {10, 0}, {10, 10}};
    const auto moved = next::translate(path, 1, 2);

    ASSERT_EQ(moved.size(), path.size());
    EXPECT_EQ(moved.front().x, 1);
}

TEST(Clipper2NextPublicHeaderTests, PublicFacadesCompileAndExecute) {
    const next::Paths64 paths{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };
    const next::Paths64 clips{
        next::Path64{{50, 50}, {150, 50}, {150, 150}, {50, 150}},
    };

    const auto builder_result = next::offset_builder{}
                                    .delta(2.0)
                                    .join(next::JoinType::Miter)
                                    .end(next::EndType::Polygon)
                                    .add(paths)
                                    .execute();
    EXPECT_FALSE(builder_result.empty());

    next::rect_clip_request64 rect_clip_request;
    rect_clip_request.rect = next::Rect64{10, 10, 90, 90};
    rect_clip_request.paths = paths;
    EXPECT_FALSE(next::rect_clip(rect_clip_request).paths.empty());

    next::rect_clip_lines_request64 line_clip_request;
    line_clip_request.rect = next::Rect64{0, 0, 10, 10};
    line_clip_request.lines = next::Paths64{next::Path64{{-5, 5}, {15, 5}}};
    EXPECT_FALSE(next::rect_clip_lines(line_clip_request).paths.empty());

    next::triangulation_request64 triangulation_request;
    triangulation_request.paths = paths;
    triangulation_request.use_delaunay = false;
    const auto triangulation_result = next::triangulate(triangulation_request);
    EXPECT_EQ(triangulation_result.status, next::TriangulateResult::success);
    EXPECT_FALSE(triangulation_result.triangles.empty());

    next::clip_request64 clip_request;
    clip_request.clip_type = next::ClipType::Union;
    clip_request.fill_rule = next::FillRule::NonZero;
    clip_request.subjects = paths;
    EXPECT_FALSE(next::clip(clip_request).closed.empty());
    clip_request.clips = clips;
    clip_request.clip_type = next::ClipType::Intersection;
    EXPECT_FALSE(next::clip(clip_request).closed.empty());
    clip_request.clip_type = next::ClipType::Difference;
    EXPECT_FALSE(next::clip(clip_request).closed.empty());
    clip_request.clip_type = next::ClipType::Xor;
    EXPECT_FALSE(next::clip(clip_request).closed.empty());

    next::offset_request64 offset_request;
    offset_request.paths = paths;
    offset_request.delta = 2.0;
    EXPECT_FALSE(next::offset(offset_request).closed.empty());

    auto borrowed_offset_request = next::borrowed_offset_request64{};
    borrowed_offset_request.paths = next::borrow_paths64(paths);
    borrowed_offset_request.delta = 2.0;
    const auto borrowed_offset_result =
        next::offset_stage_checked(borrowed_offset_request);
    ASSERT_TRUE(borrowed_offset_result.has_value());
    EXPECT_FALSE(borrowed_offset_result->paths.empty());
    EXPECT_EQ(
        borrowed_offset_result->stats.input_collection_point_writes, 0U);

    const auto translated_path = next::translate(paths.front(), 1, 2);
    const auto translated_paths = next::translate(paths, 1, 2);
    EXPECT_EQ(translated_path.front().x, paths.front().front().x + 1);
    EXPECT_EQ(translated_paths.front().front().y, paths.front().front().y + 2);

    clip_request = next::clip_request64{};
    clip_request.clip_type = next::ClipType::Union;
    clip_request.fill_rule = next::FillRule::NonZero;
    clip_request.subjects = paths;
    const auto clip_v2_result = next::clip(clip_request);
    EXPECT_FALSE(clip_v2_result.closed.empty());

    const auto clip_request_result = next::clip(clip_request);
    EXPECT_FALSE(clip_request_result.closed.empty());

    offset_request = next::offset_request64{};
    offset_request.paths = paths;
    offset_request.delta = 2.0;
    const auto offset_v2_result = next::offset(offset_request);
    EXPECT_FALSE(offset_v2_result.closed.empty());

    const auto offset_request_result = next::offset(offset_request);
    EXPECT_FALSE(offset_request_result.closed.empty());

    const std::vector<next::clip_request64> requests{clip_request, clip_request};
    const auto batch_result = next::clip_batch(requests);
    ASSERT_EQ(batch_result.size(), 2U);
    EXPECT_FALSE(batch_result.front().closed.empty());
}

TEST(Clipper2NextPublicHeaderTests, MinkowskiAndTriangulationPublicHeadersLink) {
    const next::Path64 pattern{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const next::Path64 path{{0, 0}, {20, 0}, {20, 20}, {0, 20}};

    next::minkowski_request64 request;
    request.pattern = pattern;
    request.path = path;
    request.is_closed = true;

    const auto sum = next::minkowski_sum(request);
    const auto diff = next::minkowski_difference(request);
    const auto checked_sum = next::minkowski_sum_checked(request);

    EXPECT_FALSE(sum.empty());
    EXPECT_FALSE(diff.empty());
    ASSERT_TRUE(checked_sum.has_value());
    EXPECT_FALSE(checked_sum.value().empty());

    const next::Paths64 polygon{path};
    next::triangulation_request64 triangulation_request;
    triangulation_request.paths = polygon;
    const auto triangulation_result = next::triangulate(triangulation_request);
    const auto checked_triangulation_result = next::triangulate_checked(triangulation_request);
    EXPECT_EQ(triangulation_result.status, next::TriangulateResult::success);
    EXPECT_FALSE(triangulation_result.triangles.empty());
    ASSERT_TRUE(checked_triangulation_result.has_value());
    EXPECT_EQ(checked_triangulation_result.value().status, next::TriangulateResult::success);
}
