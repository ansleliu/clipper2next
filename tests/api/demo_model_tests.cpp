#include "demo_model.h"

#include <gtest/gtest.h>

#include <cmath>

namespace demo = clipper2next::demo;

namespace {

[[nodiscard]] auto count_points(const clipper2next::Paths64& paths) -> std::size_t {
    std::size_t count{};
    for (const auto& path : paths) { count += path.size(); }
    return count;
}

}  // namespace

TEST(Clipper2NextDemoModelTests, SampleGenerationIsDeterministicForSeed) {
    demo::demo_parameters parameters;
    parameters.scene = demo::demo_scene::boolean_clip;
    parameters.seed = 77;
    parameters.path_count = 4;
    parameters.vertex_count = 6;

    const auto first = demo::run_demo(parameters);
    const auto second = demo::run_demo(parameters);

    EXPECT_EQ(first.subjects, second.subjects);
    EXPECT_EQ(first.clips, second.clips);
    EXPECT_EQ(first.closed_result, second.closed_result);
}

TEST(Clipper2NextDemoModelTests, BooleanUnionProducesClosedResultAndMetrics) {
    demo::demo_parameters parameters;
    parameters.scene = demo::demo_scene::boolean_clip;
    parameters.operation = demo::demo_operation::union_op;
    parameters.path_count = 8;
    parameters.vertex_count = 8;
    parameters.repeats = 2;

    const auto result = demo::run_demo(parameters);

    EXPECT_EQ(result.status, "ok");
    EXPECT_FALSE(result.closed_result.empty());
    EXPECT_EQ(result.metrics.repeat_count, 2U);
    EXPECT_EQ(result.metrics.input_path_count, result.subjects.size() + result.clips.size());
    EXPECT_GT(result.metrics.input_point_count, 0U);
    EXPECT_GT(result.metrics.output_path_count, 0U);
    EXPECT_GT(result.metrics.output_point_count, 0U);
    EXPECT_GT(std::fabs(result.metrics.output_area), 0.0);
}

TEST(Clipper2NextDemoModelTests, OffsetProducesClosedResultAndMetrics) {
    demo::demo_parameters parameters;
    parameters.scene = demo::demo_scene::offset;
    parameters.path_count = 3;
    parameters.vertex_count = 6;
    parameters.offset_delta = 18.0;

    const auto result = demo::run_demo(parameters);

    EXPECT_EQ(result.status, "ok");
    EXPECT_FALSE(result.closed_result.empty());
    EXPECT_EQ(result.metrics.input_path_count, result.subjects.size());
    EXPECT_GT(result.metrics.output_path_count, 0U);
    EXPECT_GT(std::fabs(result.metrics.output_area), 0.0);
}

TEST(Clipper2NextDemoModelTests, RectClipReportsPreparedReuseMode) {
    demo::demo_parameters parameters;
    parameters.scene = demo::demo_scene::rect_clip;
    parameters.path_count = 6;
    parameters.vertex_count = 5;
    parameters.rect_clip_mode = demo::demo_rect_clip_mode::prepared;

    const auto result = demo::run_demo(parameters);

    EXPECT_EQ(result.status, "ok");
    EXPECT_EQ(result.metrics.execution_mode, "prepared");
    EXPECT_FALSE(result.clip_rect.is_empty());
    EXPECT_FALSE(result.closed_result.empty());
}

TEST(Clipper2NextDemoModelTests, TriangulationReportsTriangleMetrics) {
    demo::demo_parameters parameters;
    parameters.scene = demo::demo_scene::triangulation;
    parameters.vertex_count = 12;
    parameters.use_delaunay = true;

    const auto result = demo::run_demo(parameters);

    EXPECT_EQ(result.status, "ok");
    EXPECT_FALSE(result.closed_result.empty());
    EXPECT_EQ(result.metrics.execution_mode, "delaunay");
    EXPECT_GT(result.metrics.output_path_count, 0U);
    EXPECT_GT(result.metrics.output_point_count, 0U);
}

TEST(Clipper2NextDemoModelTests, BatchClipAggregatesAllRequestOutputs) {
    demo::demo_parameters parameters;
    parameters.scene = demo::demo_scene::batch_clip;
    parameters.operation = demo::demo_operation::union_op;
    parameters.path_count = 8;
    parameters.vertex_count = 8;
    parameters.repeats = 3;

    const auto result = demo::run_demo(parameters);

    EXPECT_EQ(result.status, "ok");
    EXPECT_EQ(result.metrics.execution_mode, "batch");
    EXPECT_EQ(result.metrics.repeat_count, 3U);
    EXPECT_GT(result.closed_result.size(), 1U);
    EXPECT_EQ(result.metrics.output_path_count, result.closed_result.size() + result.open_result.size());
    EXPECT_EQ(result.metrics.output_point_count,
              count_points(result.closed_result) + count_points(result.open_result));
}
