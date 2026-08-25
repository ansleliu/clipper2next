#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "external_corpus.h"
#include "geometry_corpus_root.h"
#include "geometry_corpus_jsonl.h"
#include "path_equivalence.h"
#include "poly_tree_equivalence.h"
#include "wkt_parser.h"

#include "clipper2/clipper.h"
#include "clipper2/clipper.triangulation.h"
#include "clipper2next/batch.h"
#include "clipper2next/clipper.h"
#include "clipper2next/geometry/algorithms.h"
#include "clipper2next/geometry/scaling.h"
#include "clipper2next/geometry/translate.h"

namespace oracle = clipper2next::tests::oracle;
namespace legacy = Clipper2Lib;
namespace next = clipper2next;

namespace {

[[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path};
    if (!input) { throw std::runtime_error{"failed to open geometry corpus at " + path.string()}; }
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto overlay_operation(std::string_view operation)
    -> oracle::overlay_operation {
    constexpr std::string_view prefix = "overlay.";
    const auto prefix_position = operation.find(prefix);
    if (prefix_position != std::string_view::npos) {
        operation.remove_prefix(prefix_position + prefix.size());
    }
    return oracle::parse_overlay_operation(operation);
}

[[nodiscard]] auto parse_fill_rule(std::string_view value) -> next::FillRule;
[[nodiscard]] auto to_legacy_fill_rule(next::FillRule fill_rule) -> legacy::FillRule;

[[nodiscard]] auto execute_overlay(const oracle::geometry_corpus_record& record) -> next::Paths64 {
    next::clip_request64 request;
    request.clip_type = oracle::to_next_clip_type(overlay_operation(record.operation));
    request.fill_rule = parse_fill_rule(record.fill_rule);
    request.subjects = oracle::parse_polygonal_wkt(record.lhs_wkt);
    request.clips = oracle::parse_polygonal_wkt(record.rhs_wkt);
    request.options.preserve_collinear = record.preserve_collinear;
    request.options.reverse_solution = record.reverse_solution;
    return next::clip(request).closed;
}

[[nodiscard]] auto execute_legacy_overlay(const oracle::geometry_corpus_record& record)
    -> legacy::Paths64 {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(record.preserve_collinear);
    clipper.ReverseSolution(record.reverse_solution);
    clipper.AddSubject(oracle::to_legacy_paths(oracle::parse_polygonal_wkt(record.lhs_wkt)));
    clipper.AddClip(oracle::to_legacy_paths(oracle::parse_polygonal_wkt(record.rhs_wkt)));
    legacy::Paths64 solution;
    if (!clipper.Execute(oracle::to_legacy_clip_type(overlay_operation(record.operation)),
                         to_legacy_fill_rule(parse_fill_rule(record.fill_rule)),
                         solution)) {
        throw std::runtime_error{"legacy overlay execution failed"};
    }
    return solution;
}

[[nodiscard]] auto load_verification_records(const std::string& root_value, std::string_view profile)
    -> std::vector<oracle::geometry_corpus_record> {
    const auto path =
        oracle::verification_profile_path(std::filesystem::path{root_value}, profile);
    return oracle::parse_geometry_corpus_jsonl(read_text_file(path));
}

[[nodiscard]] auto parse_join_type(std::string_view value) -> next::JoinType {
    const auto upper = oracle::upper_ascii(value);
    if (upper == "MITER") { return next::JoinType::Miter; }
    if (upper == "SQUARE") { return next::JoinType::Square; }
    if (upper == "BEVEL") { return next::JoinType::Bevel; }
    if (upper == "ROUND") { return next::JoinType::Round; }
    throw std::runtime_error{"unsupported join_type: " + std::string{value}};
}

[[nodiscard]] auto parse_end_type(std::string_view value) -> next::EndType {
    const auto upper = oracle::upper_ascii(value);
    if (upper == "POLYGON") { return next::EndType::Polygon; }
    if (upper == "JOINED") { return next::EndType::Joined; }
    if (upper == "BUTT") { return next::EndType::Butt; }
    if (upper == "SQUARE") { return next::EndType::Square; }
    if (upper == "ROUND") { return next::EndType::Round; }
    throw std::runtime_error{"unsupported end_type: " + std::string{value}};
}

[[nodiscard]] auto parse_fill_rule(std::string_view value) -> next::FillRule {
    const auto upper = oracle::upper_ascii(value);
    if (upper == "NONZERO" || upper == "NON_ZERO") { return next::FillRule::NonZero; }
    if (upper == "EVENODD" || upper == "EVEN_ODD") { return next::FillRule::EvenOdd; }
    if (upper == "POSITIVE") { return next::FillRule::Positive; }
    if (upper == "NEGATIVE") { return next::FillRule::Negative; }
    throw std::runtime_error{"unsupported fill_rule: " + std::string{value}};
}

[[nodiscard]] auto to_legacy_fill_rule(next::FillRule fill_rule) -> legacy::FillRule {
    switch (fill_rule) {
    case next::FillRule::EvenOdd: {
        return legacy::FillRule::EvenOdd;
    }
    case next::FillRule::NonZero: {
        return legacy::FillRule::NonZero;
    }
    case next::FillRule::Positive: {
        return legacy::FillRule::Positive;
    }
    case next::FillRule::Negative: {
        return legacy::FillRule::Negative;
    }
    }
    return legacy::FillRule::EvenOdd;
}

[[nodiscard]] auto to_legacy_join_type(next::JoinType join_type) -> legacy::JoinType {
    switch (join_type) {
    case next::JoinType::Square: {
        return legacy::JoinType::Square;
    }
    case next::JoinType::Bevel: {
        return legacy::JoinType::Bevel;
    }
    case next::JoinType::Round: {
        return legacy::JoinType::Round;
    }
    case next::JoinType::Miter: {
        return legacy::JoinType::Miter;
    }
    }
    return legacy::JoinType::Miter;
}

[[nodiscard]] auto to_legacy_end_type(next::EndType end_type) -> legacy::EndType {
    switch (end_type) {
    case next::EndType::Polygon: {
        return legacy::EndType::Polygon;
    }
    case next::EndType::Joined: {
        return legacy::EndType::Joined;
    }
    case next::EndType::Butt: {
        return legacy::EndType::Butt;
    }
    case next::EndType::Square: {
        return legacy::EndType::Square;
    }
    case next::EndType::Round: {
        return legacy::EndType::Round;
    }
    }
    return legacy::EndType::Polygon;
}

[[nodiscard]] auto to_legacy_rect(const next::Rect64& rect) -> legacy::Rect64 {
    return legacy::Rect64{rect.left, rect.top, rect.right, rect.bottom};
}

[[nodiscard]] auto to_next_rect(const legacy::Rect64& rect) -> next::Rect64 {
    return next::Rect64{rect.left, rect.top, rect.right, rect.bottom};
}

[[nodiscard]] auto to_next_status(legacy::TriangulateResult status) -> next::TriangulateResult {
    switch (status) {
    case legacy::TriangulateResult::success: {
        return next::TriangulateResult::success;
    }
    case legacy::TriangulateResult::fail: {
        return next::TriangulateResult::fail;
    }
    case legacy::TriangulateResult::no_polygons: {
        return next::TriangulateResult::no_polygons;
    }
    case legacy::TriangulateResult::paths_intersect: {
        return next::TriangulateResult::paths_intersect;
    }
    }
    return next::TriangulateResult::fail;
}

[[nodiscard]] auto parse_geometry_wkt(std::string_view wkt) -> next::Paths64 {
    const auto upper = oracle::upper_ascii(wkt);
    if (upper.rfind("LINESTRING", 0) == 0 || upper.rfind("MULTILINESTRING", 0) == 0) {
        return oracle::parse_linear_wkt(wkt);
    }
    return oracle::parse_polygonal_wkt(wkt);
}

[[nodiscard]] auto execute_next_offset(const next::Paths64& paths,
                                       double delta,
                                       next::JoinType join_type,
                                       next::EndType end_type,
                                       bool preserve_collinear,
                                       bool reverse_solution) -> next::Paths64 {
    next::offset_request64 request;
    request.paths = paths;
    request.delta = delta;
    request.join_type = join_type;
    request.end_type = end_type;
    request.options.preserve_collinear = preserve_collinear;
    request.options.reverse_solution = reverse_solution;
    return next::offset(request).closed;
}

[[nodiscard]] auto execute_legacy_offset(const next::Paths64& paths,
                                         double delta,
                                         next::JoinType join_type,
                                         next::EndType end_type,
                                         bool preserve_collinear,
                                         bool reverse_solution) -> legacy::Paths64 {
    legacy::ClipperOffset offset;
    offset.PreserveCollinear(preserve_collinear);
    offset.ReverseSolution(reverse_solution);
    offset.AddPaths(oracle::to_legacy_paths(paths),
                    to_legacy_join_type(join_type),
                    to_legacy_end_type(end_type));
    legacy::Paths64 solution;
    offset.Execute(delta, solution);
    return solution;
}

[[nodiscard]] auto parse_single_linear_path(std::string_view wkt) -> next::Path64 {
    auto paths = oracle::parse_linear_wkt(wkt);
    if (paths.size() != 1U || paths.front().size() < 3U) {
        throw std::runtime_error{"Minkowski operand must contain one non-trivial path"};
    }
    return std::move(paths.front());
}

[[nodiscard]] auto execute_next_minkowski(const oracle::geometry_corpus_record& record)
    -> next::Paths64 {
    next::minkowski_request64 request;
    request.pattern = parse_single_linear_path(record.pattern_wkt);
    request.path = parse_single_linear_path(record.path_wkt);
    request.is_closed = record.is_closed;
    if (record.operation == "minkowski.sum") { return next::minkowski_sum(request); }
    if (record.operation == "minkowski.difference") {
        return next::minkowski_difference(request);
    }
    throw std::runtime_error{"unsupported Minkowski operation: " + record.operation};
}

[[nodiscard]] auto execute_legacy_minkowski(const oracle::geometry_corpus_record& record)
    -> legacy::Paths64 {
    const auto pattern = oracle::to_legacy_path(parse_single_linear_path(record.pattern_wkt));
    const auto path = oracle::to_legacy_path(parse_single_linear_path(record.path_wkt));
    if (record.operation == "minkowski.sum") {
        return legacy::MinkowskiSum(pattern, path, record.is_closed);
    }
    if (record.operation == "minkowski.difference") {
        return legacy::MinkowskiDiff(pattern, path, record.is_closed);
    }
    throw std::runtime_error{"unsupported Minkowski operation: " + record.operation};
}

auto execute_legacy_clip_tree(const oracle::geometry_corpus_record& record,
                              legacy::PolyTree64& tree,
                              legacy::Paths64& open) -> void {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(record.preserve_collinear);
    clipper.ReverseSolution(record.reverse_solution);
    clipper.AddSubject(oracle::to_legacy_paths(oracle::parse_polygonal_wkt(record.lhs_wkt)));
    clipper.AddClip(oracle::to_legacy_paths(oracle::parse_polygonal_wkt(record.rhs_wkt)));
    if (!clipper.Execute(oracle::to_legacy_clip_type(overlay_operation(record.operation)),
                         to_legacy_fill_rule(parse_fill_rule(record.fill_rule)),
                         tree,
                         open)) {
        throw std::runtime_error{"legacy tree execution failed"};
    }
}

auto assert_offset_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "offset");
    ASSERT_GE(records.size(), 128U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_TRUE(record.operation == "offset.polygon" || record.operation == "offset.open");
        ASSERT_TRUE(record.has_delta);
        EXPECT_NE(record.delta, 0.0);
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_preserve_collinear);
        ASSERT_TRUE(record.has_reverse_solution);
        ASSERT_FALSE(record.paths_wkt.empty());

        const auto join_type = parse_join_type(record.join_type);
        const auto end_type = parse_end_type(record.end_type);
        const auto input_paths = end_type == next::EndType::Polygon
                                     ? oracle::parse_polygonal_wkt(record.paths_wkt)
                                     : oracle::parse_linear_wkt(record.paths_wkt);
        EXPECT_EQ(record.operation,
                  end_type == next::EndType::Polygon ? "offset.polygon" : "offset.open");
        const auto legacy_expected = execute_legacy_offset(input_paths,
                                                           record.delta,
                                                           join_type,
                                                           end_type,
                                                           record.preserve_collinear,
                                                           record.reverse_solution);
        const auto actual = execute_next_offset(input_paths,
                                                record.delta,
                                                join_type,
                                                end_type,
                                                record.preserve_collinear,
                                                record.reverse_solution);
        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(legacy_expected, actual));
    }
}

auto assert_rectclip_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "rectclip");
    ASSERT_GE(records.size(), 128U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "rectclip.polygon");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_rect);
        ASSERT_FALSE(record.paths_wkt.empty());

        next::rect_clip_request64 request;
        request.rect = record.rect;
        request.paths = oracle::parse_polygonal_wkt(record.paths_wkt);
        const auto legacy_expected =
            legacy::RectClip(to_legacy_rect(record.rect), oracle::to_legacy_paths(request.paths));
        const auto actual = next::rect_clip(request).paths;
        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(legacy_expected, actual));
    }
}

auto assert_open_line_clip_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "rectclip-lines");
    ASSERT_GE(records.size(), 128U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "rectclip.lines");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_rect);
        ASSERT_FALSE(record.lines_wkt.empty());

        next::rect_clip_lines_request64 request;
        request.rect = record.rect;
        request.lines = oracle::parse_linear_wkt(record.lines_wkt);
        const auto legacy_expected =
            legacy::RectClipLines(to_legacy_rect(record.rect), oracle::to_legacy_paths(request.lines));
        const auto actual = next::rect_clip_lines(request).paths;
        EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(legacy_expected, actual));
    }
}

auto assert_open_path_overlay_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "open-path-overlay");
    ASSERT_GE(records.size(), 128U);
    std::set<std::string> operations;
    std::set<std::string> fill_rules;
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_FALSE(record.operation.empty());
        ASSERT_FALSE(record.lhs_wkt.empty());
        ASSERT_FALSE(record.rhs_wkt.empty());
        ASSERT_FALSE(record.fill_rule.empty());
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_preserve_collinear);
        ASSERT_TRUE(record.has_reverse_solution);

        const auto operation = overlay_operation(record.operation);
        const auto fill_rule = parse_fill_rule(record.fill_rule);
        const auto open_subjects = oracle::parse_linear_wkt(record.lhs_wkt);
        const auto clips = oracle::parse_polygonal_wkt(record.rhs_wkt);

        legacy::Clipper64 legacy_clipper;
        legacy_clipper.PreserveCollinear(record.preserve_collinear);
        legacy_clipper.ReverseSolution(record.reverse_solution);
        legacy_clipper.AddOpenSubject(oracle::to_legacy_paths(open_subjects));
        legacy_clipper.AddClip(oracle::to_legacy_paths(clips));
        legacy::Paths64 legacy_closed;
        legacy::Paths64 legacy_open;
        ASSERT_TRUE(legacy_clipper.Execute(oracle::to_legacy_clip_type(operation),
                                           to_legacy_fill_rule(fill_rule),
                                           legacy_closed,
                                           legacy_open));

        next::clip_request64 request;
        request.clip_type = oracle::to_next_clip_type(operation);
        request.fill_rule = fill_rule;
        request.open_subjects = open_subjects;
        request.clips = clips;
        request.options.preserve_collinear = record.preserve_collinear;
        request.options.reverse_solution = record.reverse_solution;
        const auto actual = next::clip(request);

        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(legacy_closed, actual.closed));
        EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(legacy_open, actual.open));
        operations.insert(record.operation);
        fill_rules.insert(record.fill_rule);
    }

    EXPECT_EQ(operations,
              (std::set<std::string>{"overlay.difference",
                                     "overlay.intersection",
                                     "overlay.union",
                                     "overlay.xor"}));
    EXPECT_EQ(fill_rules,
              (std::set<std::string>{"even_odd", "negative", "non_zero", "positive"}));
}

auto assert_minkowski_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "minkowski");
    ASSERT_GE(records.size(), 128U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_TRUE(record.operation == "minkowski.sum" ||
                    record.operation == "minkowski.difference");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_is_closed);
        ASSERT_FALSE(record.pattern_wkt.empty());
        ASSERT_FALSE(record.path_wkt.empty());

        const auto legacy_expected = execute_legacy_minkowski(record);
        const auto actual = execute_next_minkowski(record);

        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(legacy_expected, actual));
    }
}

auto assert_triangulation_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "triangulation");
    ASSERT_GE(records.size(), 128U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_TRUE(record.operation == "triangulation.sweep" ||
                    record.operation == "triangulation.delaunay");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_FALSE(record.polygon_wkt.empty());

        const auto input = oracle::parse_polygonal_wkt(record.polygon_wkt);
        ASSERT_FALSE(input.empty());
        const auto use_delaunay = record.operation == "triangulation.delaunay";

        legacy::Paths64 expected_triangles;
        const auto legacy_status = legacy::Triangulate(
            oracle::to_legacy_paths(input), expected_triangles, use_delaunay);

        next::triangulation_request64 request;
        request.paths = input;
        request.use_delaunay = use_delaunay;
        const auto actual = next::triangulate(request);
        ASSERT_EQ(to_next_status(legacy_status), actual.status);
        if (actual.status == next::TriangulateResult::success) {
            EXPECT_NO_THROW(
                oracle::assert_paths_semantically_equal(expected_triangles, actual.triangles));
        } else {
            EXPECT_TRUE(actual.triangles.empty());
        }
    }
}

auto assert_bounds_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "bounds");
    ASSERT_GE(records.size(), 32U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "geometry.bounds");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_FALSE(record.geometry_wkt.empty());

        const auto geometry = parse_geometry_wkt(record.geometry_wkt);
        ASSERT_FALSE(geometry.empty());
        const auto actual = next::bounds(geometry);
        EXPECT_EQ(actual, to_next_rect(legacy::GetBounds(oracle::to_legacy_paths(geometry))));
    }
}

[[nodiscard]] auto is_polygonal_wkt(std::string_view wkt) -> bool {
    const auto upper = oracle::upper_ascii(wkt);
    return upper.rfind("POLYGON", 0) == 0 || upper.rfind("MULTIPOLYGON", 0) == 0;
}

auto assert_simplification_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "simplification");
    ASSERT_GE(records.size(), 32U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_TRUE(record.operation == "geometry.simplify" ||
                    record.operation == "geometry.rdp");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_epsilon);
        ASSERT_GT(record.epsilon, 0.0);
        ASSERT_FALSE(record.geometry_wkt.empty());

        const auto paths = parse_geometry_wkt(record.geometry_wkt);
        ASSERT_FALSE(paths.empty());
        if (record.operation == "geometry.simplify") {
            const auto expected = legacy::SimplifyPaths(
                oracle::to_legacy_paths(paths), record.epsilon, is_polygonal_wkt(record.geometry_wkt));
            const auto actual = next::simplify_paths(
                paths, record.epsilon, is_polygonal_wkt(record.geometry_wkt));
            EXPECT_EQ(oracle::to_next_paths(expected), actual);
        } else {
            const auto expected =
                legacy::RamerDouglasPeucker(oracle::to_legacy_paths(paths), record.epsilon);
            const auto actual = next::ramer_douglas_peucker(paths, record.epsilon);
            EXPECT_EQ(oracle::to_next_paths(expected), actual);
        }
    }
}

auto assert_collinear_trimming_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "collinear-trimming");
    ASSERT_GE(records.size(), 32U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "geometry.trim_collinear");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_is_closed);
        ASSERT_FALSE(record.geometry_wkt.empty());

        const auto paths = parse_geometry_wkt(record.geometry_wkt);
        ASSERT_FALSE(paths.empty());
        for (std::size_t index = 0; index < paths.size(); ++index) {
            SCOPED_TRACE(index);
            const auto expected =
                legacy::TrimCollinear(oracle::to_legacy_path(paths[index]), !record.is_closed);
            const auto actual = next::trim_collinear(paths[index], !record.is_closed);
            EXPECT_EQ(oracle::to_next_path(expected), actual);
        }
    }
}

[[nodiscard]] auto to_next_point_in_polygon_result(legacy::PointInPolygonResult result)
    -> next::PointInPolygonResult {
    switch (result) {
    case legacy::PointInPolygonResult::IsOn: return next::PointInPolygonResult::IsOn;
    case legacy::PointInPolygonResult::IsInside: return next::PointInPolygonResult::IsInside;
    case legacy::PointInPolygonResult::IsOutside: return next::PointInPolygonResult::IsOutside;
    }
    return next::PointInPolygonResult::IsOutside;
}

auto assert_point_in_polygon_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "point-in-polygon");
    ASSERT_GE(records.size(), 32U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "geometry.point_in_polygon");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_point);
        ASSERT_FALSE(record.polygon_wkt.empty());

        const auto polygons = oracle::parse_polygonal_wkt(record.polygon_wkt);
        ASSERT_FALSE(polygons.empty());
        for (std::size_t index = 0; index < polygons.size(); ++index) {
            SCOPED_TRACE(index);
            const auto expected = legacy::PointInPolygon(
                legacy::Point64{record.point.x, record.point.y},
                oracle::to_legacy_path(polygons[index]));
            const auto actual = next::point_in_polygon(record.point, polygons[index]);
            EXPECT_EQ(to_next_point_in_polygon_result(expected), actual);
        }
    }
}

auto assert_scaling_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "scaling");
    ASSERT_GE(records.size(), 32U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "transform.scale");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_scale_factor);
        ASSERT_FALSE(record.geometry_wkt.empty());

        const auto paths = parse_geometry_wkt(record.geometry_wkt);
        ASSERT_FALSE(paths.empty());
        int legacy_error = 0;
        const auto expected = legacy::ScalePaths<int64_t, int64_t>(
            oracle::to_legacy_paths(paths), record.scale_factor, legacy_error);
        const auto actual = next::scale_paths<int64_t>(
            paths, next::scale_request{record.scale_factor, record.scale_factor});
        ASSERT_EQ(legacy_error, 0);
        ASSERT_TRUE(actual.has_value());
        EXPECT_EQ(oracle::to_next_paths(expected), actual.value());
    }
}

auto assert_translation_profile_verifies_against_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "translation");
    ASSERT_GE(records.size(), 32U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "transform.translate");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_delta_x);
        ASSERT_TRUE(record.has_delta_y);
        ASSERT_FALSE(record.geometry_wkt.empty());

        const auto paths = parse_geometry_wkt(record.geometry_wkt);
        ASSERT_FALSE(paths.empty());
        const auto expected = legacy::TranslatePaths(
            oracle::to_legacy_paths(paths), record.delta_x, record.delta_y);
        const auto actual = next::translate(paths, record.delta_x, record.delta_y);
        EXPECT_EQ(oracle::to_next_paths(expected), actual);
    }
}

auto assert_tree_profile_verifies_against_legacy(std::string_view profile) -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, profile);
    ASSERT_GE(records.size(), 128U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_FALSE(record.lhs_wkt.empty());
        ASSERT_FALSE(record.rhs_wkt.empty());
        ASSERT_FALSE(record.fill_rule.empty());
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_preserve_collinear);
        ASSERT_TRUE(record.has_reverse_solution);

        next::clip_request64 request;
        request.clip_type = oracle::to_next_clip_type(overlay_operation(record.operation));
        request.fill_rule = parse_fill_rule(record.fill_rule);
        request.subjects = oracle::parse_polygonal_wkt(record.lhs_wkt);
        request.clips = oracle::parse_polygonal_wkt(record.rhs_wkt);
        request.options.preserve_collinear = record.preserve_collinear;
        request.options.reverse_solution = record.reverse_solution;

        legacy::PolyTree64 legacy_tree;
        legacy::Paths64 legacy_open;
        execute_legacy_clip_tree(record, legacy_tree, legacy_open);
        const auto actual = next::clip_tree(request);

        EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(legacy_open, actual.open));
        EXPECT_NO_THROW(oracle::assert_poly_tree_semantically_equal(legacy_tree, actual.tree));
    }
}

auto assert_clip_tree_profile_verifies_against_legacy() -> void {
    assert_tree_profile_verifies_against_legacy("clip-tree");
}

auto assert_polytree_profile_verifies_against_legacy() -> void {
    assert_tree_profile_verifies_against_legacy("polytree");
}

[[nodiscard]] auto make_next_clip_request(
    const oracle::geometry_corpus_clip_request& profile_request) -> next::clip_request64 {
    if (!profile_request.has_preserve_collinear ||
        !profile_request.has_reverse_solution) {
        throw std::runtime_error{"batch request is missing execution options"};
    }
    next::clip_request64 request;
    request.clip_type =
        oracle::to_next_clip_type(overlay_operation(profile_request.operation));
    request.fill_rule = parse_fill_rule(profile_request.fill_rule);
    request.subjects = oracle::parse_polygonal_wkt(profile_request.lhs_wkt);
    request.clips = oracle::parse_polygonal_wkt(profile_request.rhs_wkt);
    request.options.preserve_collinear = profile_request.preserve_collinear;
    request.options.reverse_solution = profile_request.reverse_solution;
    return request;
}

struct legacy_clip_result final {
    legacy::Paths64 closed{};
    legacy::Paths64 open{};
};

[[nodiscard]] auto to_legacy_clip_type(next::ClipType clip_type) -> legacy::ClipType {
    switch (clip_type) {
    case next::ClipType::Intersection: {
        return legacy::ClipType::Intersection;
    }
    case next::ClipType::Union: {
        return legacy::ClipType::Union;
    }
    case next::ClipType::Difference: {
        return legacy::ClipType::Difference;
    }
    case next::ClipType::Xor: {
        return legacy::ClipType::Xor;
    }
    case next::ClipType::NoClip: {
        return legacy::ClipType::NoClip;
    }
    }
    return legacy::ClipType::NoClip;
}

[[nodiscard]] auto execute_legacy_clip(const next::clip_request64& request)
    -> legacy_clip_result {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(request.options.preserve_collinear);
    clipper.ReverseSolution(request.options.reverse_solution);
    clipper.AddSubject(oracle::to_legacy_paths(request.subjects));
    clipper.AddOpenSubject(oracle::to_legacy_paths(request.open_subjects));
    clipper.AddClip(oracle::to_legacy_paths(request.clips));

    legacy_clip_result result;
    if (!clipper.Execute(to_legacy_clip_type(request.clip_type),
                         to_legacy_fill_rule(request.fill_rule),
                         result.closed,
                         result.open)) {
        throw std::runtime_error{"legacy batch scalar request failed"};
    }
    return result;
}

auto assert_batch_profile_verifies_against_scalar_and_legacy() -> void {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto records = load_verification_records(root_value, "batch");
    ASSERT_GE(records.size(), 128U);
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_EQ(record.operation, "batch.clip");
        ASSERT_EQ(record.scenario, "batch.scalar_next_legacy");
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_GE(record.requests.size(), 4U);

        std::vector<next::clip_request64> requests;
        requests.reserve(record.requests.size());
        for (const auto& profile_request : record.requests) {
            requests.push_back(make_next_clip_request(profile_request));
        }
        const auto batch = next::clip_batch(requests);
        ASSERT_EQ(batch.size(), requests.size());

        for (std::size_t index = 0; index < requests.size(); ++index) {
            SCOPED_TRACE(index);
            const auto legacy_scalar = execute_legacy_clip(requests[index]);
            const auto next_scalar = next::clip(requests[index]);
            EXPECT_NO_THROW(
                oracle::assert_paths_semantically_equal(legacy_scalar.closed,
                                                        next_scalar.closed));
            EXPECT_NO_THROW(
                oracle::assert_open_paths_exactly_equal(legacy_scalar.open,
                                                        next_scalar.open));
            EXPECT_NO_THROW(
                oracle::assert_paths_semantically_equal(next_scalar.closed,
                                                        batch[index].closed));
            EXPECT_NO_THROW(
                oracle::assert_open_paths_exactly_equal(next_scalar.open,
                                                        batch[index].open));
        }
    }
}

}  // namespace

TEST(ExternalGeometryCorpus, OverlayOperationParserAcceptsCanonicalSymmetricDifference) {
    EXPECT_EQ(oracle::parse_overlay_operation("symmetric_difference"),
              oracle::overlay_operation::xor_);
}

TEST(ExternalGeometryCorpus, OverlayVerificationCorpusExistsWhenEnabled) {
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto path =
        oracle::verification_profile_path(std::filesystem::path{root_value}, "overlay");
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
}

TEST(ExternalGeometryCorpus, OverlayVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("overlay")
    const auto root_value = oracle::geometry_corpus_root();
    if (std::string_view{root_value}.empty()) {
        GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
    }

    const auto path =
        oracle::verification_profile_path(std::filesystem::path{root_value}, "overlay");
    const auto records = oracle::parse_geometry_corpus_jsonl(read_text_file(path));
    ASSERT_GE(records.size(), 128U);

    std::set<std::string> operations;
    for (const auto& record : records) {
        SCOPED_TRACE(record.id);
        ASSERT_EQ(record.profile, "verification");
        ASSERT_FALSE(record.operation.empty());
        ASSERT_FALSE(record.lhs_wkt.empty());
        ASSERT_FALSE(record.rhs_wkt.empty());
        ASSERT_FALSE(record.fill_rule.empty());
        ASSERT_EQ(record.expected_relation, "strict-legacy-runtime");
        ASSERT_TRUE(record.has_preserve_collinear);
        ASSERT_TRUE(record.has_reverse_solution);

        const auto legacy_expected = execute_legacy_overlay(record);
        const auto actual = execute_overlay(record);
        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(legacy_expected, actual));

        operations.insert(record.operation);
    }

    EXPECT_TRUE(operations.contains("overlay.intersection"));
    EXPECT_TRUE(operations.contains("overlay.union"));
    EXPECT_TRUE(operations.contains("overlay.difference"));
    EXPECT_TRUE(operations.contains("overlay.xor"));
}

TEST(ExternalGeometryCorpus, OffsetVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("offset")
    assert_offset_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, RectClipVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("rectclip")
    assert_rectclip_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, OpenLineClipVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("rectclip-lines")
    assert_open_line_clip_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, OpenPathOverlayVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("open-path-overlay")
    assert_open_path_overlay_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, MinkowskiVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("minkowski")
    assert_minkowski_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, TriangulationVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("triangulation")
    assert_triangulation_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, BoundsVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("bounds")
    assert_bounds_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, SimplificationVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("simplification")
    assert_simplification_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, CollinearTrimmingVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("collinear-trimming")
    assert_collinear_trimming_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, PointInPolygonVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("point-in-polygon")
    assert_point_in_polygon_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, ScalingVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("scaling")
    assert_scaling_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, TranslationVerificationCorpusExecutesAgainstLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("translation")
    assert_translation_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, ClipTreeVerificationCorpusExecutesAgainstLegacyTreeShape) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("clip-tree")
    assert_clip_tree_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, PolyTreeVerificationCorpusExecutesAgainstLegacyTreeShape) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("polytree")
    assert_polytree_profile_verifies_against_legacy();
}

TEST(ExternalGeometryCorpus, BatchVerificationCorpusExecutesAgainstScalarAndLegacy) {
    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("batch")
    assert_batch_profile_verifies_against_scalar_and_legacy();
}
