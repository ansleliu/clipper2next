#include <gtest/gtest.h>

#include <filesystem>

#include "geometry_corpus_jsonl.h"

TEST(GeometryCorpusJsonl, ParsesTopLevelStringField) {
    const std::string record_json = R"json({"id":"unit"})json";
    EXPECT_EQ(clipper2next::tests::oracle::json_string_field(record_json, "id"), "unit");
    EXPECT_EQ(clipper2next::tests::oracle::parse_geometry_corpus_jsonl(record_json).size(), 1U);
}

TEST(GeometryCorpusJsonl, ParsesSingleVerificationRecord) {
    constexpr std::string_view record_json =
        R"json({"id":"unit","profile":"verification","operation":"overlay.intersection","inputs":{"lhs_wkt":"POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))","rhs_wkt":"POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))"},"expected_wkt":"POLYGON ((2 2, 3 2, 3 3, 2 3, 2 2))","expected":{"wkt":"POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))"},"tolerance":{"coordinate":0},"semantics":{"compare":"canonical_polygonal"},"reference_engines":[],"canonical_digest":"abc","source":{"source_id":"curated"},"tags":["unit"]})json";
    EXPECT_EQ(clipper2next::tests::oracle::json_string_field(record_json, "id"), "unit");
    const auto records = clipper2next::tests::oracle::parse_geometry_corpus_jsonl(
        record_json);

    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().id, "unit");
    EXPECT_EQ(records.front().profile, "verification");
    EXPECT_EQ(records.front().operation, "overlay.intersection");
    EXPECT_EQ(records.front().lhs_wkt, "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
    EXPECT_EQ(records.front().rhs_wkt, "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
    EXPECT_EQ(records.front().expected_wkt, "POLYGON ((2 2, 3 2, 3 3, 2 3, 2 2))");
}

TEST(GeometryCorpusJsonl, ParsesProfileSpecificInputsAndRectMetadata) {
    const auto records = clipper2next::tests::oracle::parse_geometry_corpus_jsonl(
        R"json({"id":"offset","profile":"verification","operation":"offset.polygon","inputs":{"paths_wkt":"POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))","delta":2.0,"join_type":"round","end_type":"polygon"},"expected":{"relation":"strict-legacy-runtime"},"tolerance":{"coordinate":0}})json"
        "\n"
        R"json({"id":"line","profile":"verification","operation":"rectclip.lines","inputs":{"lines_wkt":"LINESTRING (0 0, 10 10)","rect":{"left":-1.0,"top":-2.0,"right":11.0,"bottom":12.0}},"expected":{"relation":"strict-legacy-runtime"},"tolerance":{"coordinate":0}})json"
        "\n"
        R"json({"id":"minkowski","profile":"verification","operation":"minkowski.difference","inputs":{"pattern_wkt":"LINESTRING (0 0, 10 0, 0 10)","path_wkt":"LINESTRING (20 20, 30 20, 20 30)","is_closed":false},"expected":{"relation":"strict-legacy-runtime"},"tolerance":{"coordinate":0}})json");

    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[0].paths_wkt, "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))");
    EXPECT_EQ(records[0].expected_relation, "strict-legacy-runtime");
    EXPECT_EQ(records[0].join_type, "round");
    EXPECT_EQ(records[0].end_type, "polygon");
    EXPECT_DOUBLE_EQ(records[0].delta, 2.0);

    EXPECT_EQ(records[1].lines_wkt, "LINESTRING (0 0, 10 10)");
    ASSERT_TRUE(records[1].has_rect);
    EXPECT_EQ(records[1].rect.left, -1);
    EXPECT_EQ(records[1].rect.top, -2);
    EXPECT_EQ(records[1].rect.right, 11);
    EXPECT_EQ(records[1].rect.bottom, 12);

    EXPECT_EQ(records[2].pattern_wkt, "LINESTRING (0 0, 10 0, 0 10)");
    EXPECT_EQ(records[2].path_wkt, "LINESTRING (20 20, 30 20, 20 30)");
    ASSERT_TRUE(records[2].has_is_closed);
    EXPECT_FALSE(records[2].is_closed);
}

TEST(GeometryCorpusJsonl, ParsesOverlayOptionsAndBatchRequests) {
    const auto records = clipper2next::tests::oracle::parse_geometry_corpus_jsonl(
        R"json({"id":"batch","profile":"verification","operation":"batch.clip","scenario":"batch.scalar_next_legacy","inputs":{"comparison_modes":["legacy_scalar","next_scalar","next_batch"],"requests":[{"operation":"overlay.intersection","lhs_wkt":"POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))","rhs_wkt":"POLYGON ((5 5, 15 5, 15 15, 5 15, 5 5))","fill_rule":"non_zero","preserve_collinear":true,"reverse_solution":false},{"operation":"overlay.xor","lhs_wkt":"POLYGON ((20 20, 30 20, 30 30, 20 30, 20 20))","rhs_wkt":"POLYGON ((25 25, 35 25, 35 35, 25 35, 25 25))","fill_rule":"even_odd","preserve_collinear":false,"reverse_solution":true}]},"expected":{"relation":"strict-legacy-runtime"}})json");

    ASSERT_EQ(records.size(), 1U);
    const auto& record = records.front();
    EXPECT_EQ(record.operation, "batch.clip");
    EXPECT_EQ(record.scenario, "batch.scalar_next_legacy");
    ASSERT_EQ(record.requests.size(), 2U);
    EXPECT_EQ(record.requests[0].operation, "overlay.intersection");
    EXPECT_EQ(record.requests[0].fill_rule, "non_zero");
    ASSERT_TRUE(record.requests[0].has_preserve_collinear);
    EXPECT_TRUE(record.requests[0].preserve_collinear);
    ASSERT_TRUE(record.requests[0].has_reverse_solution);
    EXPECT_FALSE(record.requests[0].reverse_solution);
    EXPECT_EQ(record.requests[1].operation, "overlay.xor");
    EXPECT_EQ(record.requests[1].fill_rule, "even_odd");
    EXPECT_FALSE(record.requests[1].preserve_collinear);
    EXPECT_TRUE(record.requests[1].reverse_solution);
}

TEST(GeometryCorpusJsonl, ParsesGeometryAlgorithmAndTransformInputs) {
    const auto records = clipper2next::tests::oracle::parse_geometry_corpus_jsonl(
        R"json({"id":"simplify","inputs":{"epsilon":1.5,"geometry_wkt":"LINESTRING (0 0, 1 1)"}})json"
        "\n"
        R"json({"id":"trim","inputs":{"is_closed":true,"geometry_wkt":"POLYGON ((0 0, 1 0, 0 0))"}})json"
        "\n"
        R"json({"id":"pip","inputs":{"point":{"x":-7,"y":11},"polygon_wkt":"POLYGON ((0 0, 1 0, 0 0))"}})json"
        "\n"
        R"json({"id":"scale","inputs":{"scale_factor":2.25,"geometry_wkt":"LINESTRING (0 0, 1 1)"}})json"
        "\n"
        R"json({"id":"translate","inputs":{"delta_x":17,"delta_y":-23,"geometry_wkt":"LINESTRING (0 0, 1 1)"}})json");

    ASSERT_EQ(records.size(), 5U);
    ASSERT_TRUE(records[0].has_epsilon);
    EXPECT_DOUBLE_EQ(records[0].epsilon, 1.5);
    ASSERT_TRUE(records[1].has_is_closed);
    EXPECT_TRUE(records[1].is_closed);
    ASSERT_TRUE(records[2].has_point);
    EXPECT_EQ(records[2].point, clipper2next::Point64(-7, 11));
    ASSERT_TRUE(records[3].has_scale_factor);
    EXPECT_DOUBLE_EQ(records[3].scale_factor, 2.25);
    ASSERT_TRUE(records[4].has_delta_x);
    ASSERT_TRUE(records[4].has_delta_y);
    EXPECT_EQ(records[4].delta_x, 17);
    EXPECT_EQ(records[4].delta_y, -23);
}

TEST(GeometryCorpusJsonl, BuildsVerificationPathFromRoot) {
    const auto root = std::filesystem::path{"/data/geometry"};
    const auto path = clipper2next::tests::oracle::verification_profile_path(root, "overlay");
    EXPECT_EQ(path.generic_string(), "/data/geometry/normalized/verification/overlay.jsonl");
}

TEST(GeometryCorpusJsonl, BuildsBenchmarkPathFromRoot) {
    const auto root = std::filesystem::path{"/data/geometry"};
    const auto path = clipper2next::tests::oracle::benchmark_profile_path(root, "overlay");
    EXPECT_EQ(path.generic_string(), "/data/geometry/normalized/benchmark/overlay.jsonl");
}
