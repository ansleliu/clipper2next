#include <benchmark/benchmark.h>

#include "../../tests/oracle/external_corpus.h"
#include "../../tests/oracle/geometry_corpus_jsonl.h"
#include "../../tests/oracle/path_equivalence.h"
#include "../../tests/oracle/poly_tree_equivalence.h"
#include "../../tests/oracle/wkt_parser.h"

#include "clipper2/clipper.h"
#include "clipper2/clipper.triangulation.h"
#include "clipper2next/batch.h"
#include "clipper2next/clipper.h"

#if defined(CLIPPER2NEXT_MSVC_PGO_INSTRUMENTED)
#include <pgobootrun.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

namespace {

constexpr std::string_view source_name{"geometry_corpus"};

struct parsed_case final {
    oracle::overlay_operation operation{oracle::overlay_operation::intersection};
    legacy::Paths64 legacy_subjects{};
    legacy::Paths64 legacy_clips{};
    next::clip_request64 next_request{};
    next::prepared_clip_request64 next_prepared_request{};
};

struct rectclip_case final {
    legacy::Rect64 legacy_rect{};
    legacy::Paths64 legacy_paths{};
    next::rect_clip_request64 next_request{};
};

struct line_clip_case final {
    legacy::Rect64 legacy_rect{};
    legacy::Paths64 legacy_lines{};
    next::rect_clip_lines_request64 next_request{};
};

struct open_path_overlay_case final {
    oracle::overlay_operation operation{oracle::overlay_operation::intersection};
    legacy::FillRule legacy_fill_rule{legacy::FillRule::EvenOdd};
    legacy::Paths64 legacy_open_subjects{};
    legacy::Paths64 legacy_clips{};
    next::clip_request64 next_request{};
};

struct offset_case final {
    legacy::Paths64 legacy_paths{};
    next::offset_request64 next_request{};
};

struct triangulation_case final {
    legacy::Paths64 legacy_paths{};
    next::triangulation_request64 next_request{};
};

struct bounds_case final {
    legacy::Paths64 legacy_paths{};
    next::Paths64 next_paths{};
};

struct minkowski_case final {
    bool is_difference{};
    legacy::Path64 legacy_pattern{};
    legacy::Path64 legacy_path{};
    next::minkowski_request64 next_request{};
};

struct tree_case final {
    oracle::overlay_operation operation{oracle::overlay_operation::intersection};
    legacy::FillRule legacy_fill_rule{legacy::FillRule::EvenOdd};
    legacy::Paths64 legacy_subjects{};
    legacy::Paths64 legacy_clips{};
    next::clip_request64 next_request{};
};

struct batch_case final {
    std::vector<parsed_case> scalar_requests{};
    std::vector<next::clip_request64> next_requests{};
};

#if defined(CLIPPER2NEXT_MSVC_PGO_INSTRUMENTED)
struct current_training_profiles final {
    std::vector<parsed_case> overlay{};
    std::vector<rectclip_case> rectclip{};
    std::vector<line_clip_case> line_clip{};
    std::vector<open_path_overlay_case> open_path_overlay{};
    std::vector<offset_case> offset{};
    std::vector<triangulation_case> triangulation{};
    std::vector<bounds_case> bounds{};
    std::vector<minkowski_case> minkowski{};
    std::vector<tree_case> polytree{};
    std::vector<tree_case> clip_tree{};
    std::vector<batch_case> batch{};
};
#endif

using grouped_cases = std::map<std::string, std::vector<parsed_case>>;
[[nodiscard]] auto environment_variable(const char* name) -> std::string {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, name) != 0 || value == nullptr) { return {}; }
    std::string result{value};
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
#endif
}

[[nodiscard]] auto geometry_corpus_root() -> std::string {
    return environment_variable("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT");
}

[[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error{"failed to open geometry corpus profile at " + path.string()};
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto load_benchmark_records(const std::filesystem::path& root,
                                          std::string_view profile)
    -> std::vector<oracle::geometry_corpus_record> {
    const auto path = oracle::benchmark_profile_path(root, profile);
    auto records = oracle::parse_geometry_corpus_jsonl(read_text_file(path));
    if (records.empty()) {
        throw std::runtime_error{"geometry corpus benchmark profile has no cases: " +
                                 path.string()};
    }
    for (const auto& record : records) {
        if (record.profile != "benchmark") {
            throw std::runtime_error{"non-benchmark record in benchmark profile: " + record.id};
        }
    }
    return records;
}

[[nodiscard]] auto to_legacy_rect(const next::Rect64& rect) -> legacy::Rect64 {
    return legacy::Rect64{rect.left, rect.top, rect.right, rect.bottom};
}

[[nodiscard]] auto overlay_operation_from_profile_name(std::string_view name)
    -> oracle::overlay_operation {
    constexpr std::string_view overlay_prefix{"overlay."};
    constexpr std::string_view polytree_prefix{"polytree.overlay."};
    constexpr std::string_view clip_tree_prefix{"clip-tree.overlay."};
    if (name.rfind(overlay_prefix, 0) == 0) { name.remove_prefix(overlay_prefix.size()); }
    if (name.rfind(polytree_prefix, 0) == 0) { name.remove_prefix(polytree_prefix.size()); }
    if (name.rfind(clip_tree_prefix, 0) == 0) { name.remove_prefix(clip_tree_prefix.size()); }
    return oracle::parse_overlay_operation(name);
}

[[nodiscard]] auto fill_rule_from_profile_name(std::string_view name) -> next::FillRule {
    const auto upper = oracle::upper_ascii(name);
    if (upper == "EVENODD" || upper == "EVEN_ODD") { return next::FillRule::EvenOdd; }
    if (upper == "NONZERO" || upper == "NON_ZERO") { return next::FillRule::NonZero; }
    if (upper == "POSITIVE") { return next::FillRule::Positive; }
    if (upper == "NEGATIVE") { return next::FillRule::Negative; }
    throw std::runtime_error{"unsupported fill rule: " + std::string{name}};
}

[[nodiscard]] auto join_type_from_profile_name(std::string_view name) -> next::JoinType {
    const auto upper = oracle::upper_ascii(name);
    if (upper == "MITER") { return next::JoinType::Miter; }
    if (upper == "SQUARE") { return next::JoinType::Square; }
    if (upper == "BEVEL") { return next::JoinType::Bevel; }
    if (upper == "ROUND") { return next::JoinType::Round; }
    throw std::runtime_error{"unsupported join type: " + std::string{name}};
}

[[nodiscard]] auto end_type_from_profile_name(std::string_view name) -> next::EndType {
    const auto upper = oracle::upper_ascii(name);
    if (upper == "POLYGON") { return next::EndType::Polygon; }
    if (upper == "JOINED") { return next::EndType::Joined; }
    if (upper == "BUTT") { return next::EndType::Butt; }
    if (upper == "SQUARE") { return next::EndType::Square; }
    if (upper == "ROUND") { return next::EndType::Round; }
    throw std::runtime_error{"unsupported end type: " + std::string{name}};
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

[[nodiscard]] auto parse_geometry_paths(std::string_view wkt) -> next::Paths64 {
    if (wkt.rfind("LINESTRING", 0) == 0 || wkt.rfind("MULTILINESTRING", 0) == 0) {
        return oracle::parse_linear_wkt(wkt);
    }
    return oracle::parse_polygonal_wkt(wkt);
}

[[nodiscard]] auto parse_single_linear_path(std::string_view wkt) -> next::Path64 {
    auto paths = oracle::parse_linear_wkt(wkt);
    if (paths.size() != 1U || paths.front().size() < 3U) {
        throw std::runtime_error{"Minkowski operand must contain one non-trivial path"};
    }
    return std::move(paths.front());
}

[[nodiscard]] auto make_parsed_case(oracle::overlay_operation operation,
                                    next::Paths64 subjects,
                                    next::Paths64 clips,
                                    next::FillRule fill_rule,
                                    bool preserve_collinear,
                                    bool reverse_solution) -> parsed_case {
    parsed_case parsed;
    parsed.operation = operation;
    parsed.legacy_subjects = oracle::to_legacy_paths(subjects);
    parsed.legacy_clips = oracle::to_legacy_paths(clips);
    parsed.next_request.clip_type = oracle::to_next_clip_type(operation);
    parsed.next_request.fill_rule = fill_rule;
    parsed.next_request.subjects = std::move(subjects);
    parsed.next_request.clips = std::move(clips);
    parsed.next_request.options.preserve_collinear = preserve_collinear;
    parsed.next_request.options.reverse_solution = reverse_solution;
    parsed.next_prepared_request = next::prepare_clip_request(parsed.next_request);
    return parsed;
}

[[nodiscard]] auto load_geometry_corpus_profile_cases(const std::filesystem::path& root)
    -> grouped_cases {
    grouped_cases grouped;
    const auto records = load_benchmark_records(root, "overlay");
    auto& cases = grouped[std::string{source_name}];
    for (const auto& record : records) {
        if (record.operation.empty() || record.lhs_wkt.empty() || record.rhs_wkt.empty() ||
            record.fill_rule.empty() || !record.has_preserve_collinear ||
            !record.has_reverse_solution) {
            throw std::runtime_error{"invalid geometry corpus benchmark record: " + record.id};
        }

        auto subjects = oracle::parse_polygonal_wkt(record.lhs_wkt);
        auto clips = oracle::parse_polygonal_wkt(record.rhs_wkt);
        cases.push_back(make_parsed_case(overlay_operation_from_profile_name(record.operation),
                                         std::move(subjects),
                                         std::move(clips),
                                         fill_rule_from_profile_name(record.fill_rule),
                                         record.preserve_collinear,
                                         record.reverse_solution));
    }
    return grouped;
}

[[nodiscard]] auto load_rectclip_cases(const std::filesystem::path& root)
    -> std::vector<rectclip_case> {
    std::vector<rectclip_case> cases;
    for (const auto& record : load_benchmark_records(root, "rectclip")) {
        if (record.paths_wkt.empty() || !record.has_rect) {
            throw std::runtime_error{"invalid rectclip benchmark record: " + record.id};
        }
        const auto paths = oracle::parse_polygonal_wkt(record.paths_wkt);
        rectclip_case test_case;
        test_case.legacy_rect = to_legacy_rect(record.rect);
        test_case.legacy_paths = oracle::to_legacy_paths(paths);
        test_case.next_request.rect = record.rect;
        test_case.next_request.paths = paths;
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_line_clip_cases(const std::filesystem::path& root)
    -> std::vector<line_clip_case> {
    std::vector<line_clip_case> cases;
    for (const auto& record : load_benchmark_records(root, "rectclip-lines")) {
        if (record.lines_wkt.empty() || !record.has_rect) {
            throw std::runtime_error{"invalid rectclip-lines benchmark record: " + record.id};
        }
        const auto lines = oracle::parse_linear_wkt(record.lines_wkt);
        line_clip_case test_case;
        test_case.legacy_rect = to_legacy_rect(record.rect);
        test_case.legacy_lines = oracle::to_legacy_paths(lines);
        test_case.next_request.rect = record.rect;
        test_case.next_request.lines = lines;
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_open_path_overlay_cases(const std::filesystem::path& root)
    -> std::vector<open_path_overlay_case> {
    std::vector<open_path_overlay_case> cases;
    const auto records = load_benchmark_records(root, "open-path-overlay");
    cases.reserve(records.size());
    for (const auto& record : records) {
        if (record.operation.empty() || record.lhs_wkt.empty() || record.rhs_wkt.empty() ||
            record.fill_rule.empty() || !record.has_preserve_collinear ||
            !record.has_reverse_solution) {
            throw std::runtime_error{"invalid open-path overlay benchmark record: " + record.id};
        }

        const auto operation = overlay_operation_from_profile_name(record.operation);
        const auto fill_rule = fill_rule_from_profile_name(record.fill_rule);
        auto open_subjects = oracle::parse_linear_wkt(record.lhs_wkt);
        auto clips = oracle::parse_polygonal_wkt(record.rhs_wkt);
        open_path_overlay_case test_case;
        test_case.operation = operation;
        test_case.legacy_fill_rule = to_legacy_fill_rule(fill_rule);
        test_case.legacy_open_subjects = oracle::to_legacy_paths(open_subjects);
        test_case.legacy_clips = oracle::to_legacy_paths(clips);
        test_case.next_request.clip_type = oracle::to_next_clip_type(operation);
        test_case.next_request.fill_rule = fill_rule;
        test_case.next_request.open_subjects = std::move(open_subjects);
        test_case.next_request.clips = std::move(clips);
        test_case.next_request.options.preserve_collinear = record.preserve_collinear;
        test_case.next_request.options.reverse_solution = record.reverse_solution;
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_offset_cases(const std::filesystem::path& root)
    -> std::vector<offset_case> {
    std::vector<offset_case> cases;
    for (const auto& record : load_benchmark_records(root, "offset")) {
        if (record.paths_wkt.empty() || !record.has_delta || record.join_type.empty() ||
            record.end_type.empty() || !record.has_preserve_collinear ||
            !record.has_reverse_solution) {
            throw std::runtime_error{"invalid offset benchmark record: " + record.id};
        }
        const auto paths = parse_geometry_paths(record.paths_wkt);
        offset_case test_case;
        test_case.legacy_paths = oracle::to_legacy_paths(paths);
        test_case.next_request.paths = paths;
        test_case.next_request.delta = record.delta;
        test_case.next_request.join_type = join_type_from_profile_name(record.join_type);
        test_case.next_request.end_type = end_type_from_profile_name(record.end_type);
        test_case.next_request.options.preserve_collinear = record.preserve_collinear;
        test_case.next_request.options.reverse_solution = record.reverse_solution;
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_triangulation_cases(const std::filesystem::path& root)
    -> std::vector<triangulation_case> {
    std::vector<triangulation_case> cases;
    for (const auto& record : load_benchmark_records(root, "triangulation")) {
        const bool use_delaunay = record.operation == "triangulation.delaunay";
        if (record.polygon_wkt.empty() ||
            (!use_delaunay && record.operation != "triangulation.sweep")) {
            throw std::runtime_error{"invalid triangulation benchmark record: " + record.id};
        }
        const auto paths = oracle::parse_polygonal_wkt(record.polygon_wkt);
        triangulation_case test_case;
        test_case.legacy_paths = oracle::to_legacy_paths(paths);
        test_case.next_request.paths = paths;
        test_case.next_request.use_delaunay = use_delaunay;
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_bounds_cases(const std::filesystem::path& root)
    -> std::vector<bounds_case> {
    std::vector<bounds_case> cases;
    for (const auto& record : load_benchmark_records(root, "bounds")) {
        if (record.operation != "geometry.bounds" || record.geometry_wkt.empty()) {
            throw std::runtime_error{"invalid bounds benchmark record: " + record.id};
        }
        auto paths = parse_geometry_paths(record.geometry_wkt);
        bounds_case test_case;
        test_case.legacy_paths = oracle::to_legacy_paths(paths);
        test_case.next_paths = std::move(paths);
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_minkowski_cases(const std::filesystem::path& root)
    -> std::vector<minkowski_case> {
    std::vector<minkowski_case> cases;
    for (const auto& record : load_benchmark_records(root, "minkowski")) {
        const bool is_difference = record.operation == "minkowski.difference";
        if (record.pattern_wkt.empty() || record.path_wkt.empty() || !record.has_is_closed ||
            (!is_difference && record.operation != "minkowski.sum")) {
            throw std::runtime_error{"invalid Minkowski benchmark record: " + record.id};
        }
        const auto pattern = parse_single_linear_path(record.pattern_wkt);
        const auto path = parse_single_linear_path(record.path_wkt);
        minkowski_case test_case;
        test_case.is_difference = is_difference;
        test_case.legacy_pattern = oracle::to_legacy_path(pattern);
        test_case.legacy_path = oracle::to_legacy_path(path);
        test_case.next_request.pattern = pattern;
        test_case.next_request.path = path;
        test_case.next_request.is_closed = record.is_closed;
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_tree_cases(const std::filesystem::path& root, std::string_view profile)
    -> std::vector<tree_case> {
    std::vector<tree_case> cases;
    for (const auto& record : load_benchmark_records(root, profile)) {
        if (record.operation.empty() || record.lhs_wkt.empty() || record.rhs_wkt.empty() ||
            record.fill_rule.empty() || !record.has_preserve_collinear ||
            !record.has_reverse_solution) {
            throw std::runtime_error{"invalid tree benchmark record: " + record.id};
        }
        auto subjects = oracle::parse_polygonal_wkt(record.lhs_wkt);
        auto clips = oracle::parse_polygonal_wkt(record.rhs_wkt);
        tree_case test_case;
        test_case.operation = overlay_operation_from_profile_name(record.operation);
        test_case.legacy_fill_rule =
            to_legacy_fill_rule(fill_rule_from_profile_name(record.fill_rule));
        test_case.legacy_subjects = oracle::to_legacy_paths(subjects);
        test_case.legacy_clips = oracle::to_legacy_paths(clips);
        test_case.next_request.clip_type = oracle::to_next_clip_type(test_case.operation);
        test_case.next_request.fill_rule = fill_rule_from_profile_name(record.fill_rule);
        test_case.next_request.subjects = std::move(subjects);
        test_case.next_request.clips = std::move(clips);
        test_case.next_request.options.preserve_collinear = record.preserve_collinear;
        test_case.next_request.options.reverse_solution = record.reverse_solution;
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_batch_cases(const std::filesystem::path& root) -> std::vector<batch_case> {
    std::vector<batch_case> cases;
    for (const auto& record : load_benchmark_records(root, "batch")) {
        if (record.operation != "batch.clip" || record.requests.empty()) {
            throw std::runtime_error{"invalid batch benchmark record: " + record.id};
        }

        batch_case test_case;
        test_case.scalar_requests.reserve(record.requests.size());
        test_case.next_requests.reserve(record.requests.size());
        for (const auto& request : record.requests) {
            if (request.operation.empty() || request.lhs_wkt.empty() || request.rhs_wkt.empty() ||
                request.fill_rule.empty() || !request.has_preserve_collinear ||
                !request.has_reverse_solution) {
                throw std::runtime_error{"invalid request in batch benchmark record: " + record.id};
            }
            auto scalar = make_parsed_case(overlay_operation_from_profile_name(request.operation),
                                           oracle::parse_polygonal_wkt(request.lhs_wkt),
                                           oracle::parse_polygonal_wkt(request.rhs_wkt),
                                           fill_rule_from_profile_name(request.fill_rule),
                                           request.preserve_collinear,
                                           request.reverse_solution);
            test_case.next_requests.push_back(scalar.next_request);
            test_case.scalar_requests.push_back(std::move(scalar));
        }
        cases.push_back(std::move(test_case));
    }
    return cases;
}

[[nodiscard]] auto load_cases() -> grouped_cases {
    const auto root_value = geometry_corpus_root();
    if (root_value.empty()) { return {}; }
    return load_geometry_corpus_profile_cases(std::filesystem::path{root_value});
}

[[nodiscard]] auto load_source_cases(std::string_view source) -> std::vector<parsed_case> {
    const auto grouped = load_cases();
    const auto found = grouped.find(std::string{source});
    if (found == grouped.end()) { return {}; }
    return found->second;
}

[[nodiscard]] auto execute_legacy(const parsed_case& test_case) -> legacy::Paths64 {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(test_case.next_request.options.preserve_collinear);
    clipper.ReverseSolution(test_case.next_request.options.reverse_solution);
    clipper.AddSubject(test_case.legacy_subjects);
    clipper.AddClip(test_case.legacy_clips);
    legacy::Paths64 solution;
    clipper.Execute(oracle::to_legacy_clip_type(test_case.operation),
                    to_legacy_fill_rule(test_case.next_request.fill_rule),
                    solution);
    return solution;
}

[[nodiscard]] auto execute_next(const parsed_case& test_case) -> next::Paths64 {
    return next::clip(test_case.next_request).closed;
}

[[nodiscard]] auto operation_suffix(oracle::overlay_operation operation) -> std::string_view {
    switch (operation) {
    case oracle::overlay_operation::intersection:
        return "intersection";
    case oracle::overlay_operation::union_:
        return "union";
    case oracle::overlay_operation::difference:
        return "difference";
    case oracle::overlay_operation::xor_:
        return "xor";
    }
    return "unknown";
}

[[nodiscard]] auto filter_cases_by_operation(const std::vector<parsed_case>& cases,
                                             oracle::overlay_operation operation)
    -> std::vector<parsed_case> {
    std::vector<parsed_case> filtered;
    for (const auto& test_case : cases) {
        if (test_case.operation == operation) { filtered.push_back(test_case); }
    }
    return filtered;
}

[[nodiscard]] auto execute_next_prepared(const parsed_case& test_case) -> next::Paths64 {
    return next::clip(test_case.next_prepared_request).closed;
}

[[nodiscard]] auto execute_legacy_rectclip(const rectclip_case& test_case) -> legacy::Paths64 {
    return legacy::RectClip(test_case.legacy_rect, test_case.legacy_paths);
}

[[nodiscard]] auto execute_next_rectclip(const rectclip_case& test_case) -> next::Paths64 {
    return next::rect_clip(test_case.next_request).paths;
}

[[nodiscard]] auto execute_legacy_line_clip(const line_clip_case& test_case) -> legacy::Paths64 {
    return legacy::RectClipLines(test_case.legacy_rect, test_case.legacy_lines);
}

[[nodiscard]] auto execute_next_line_clip(const line_clip_case& test_case) -> next::Paths64 {
    return next::rect_clip_lines(test_case.next_request).paths;
}

[[nodiscard]] auto execute_legacy_open_path_overlay(const open_path_overlay_case& test_case)
    -> legacy::Paths64 {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(test_case.next_request.options.preserve_collinear);
    clipper.ReverseSolution(test_case.next_request.options.reverse_solution);
    clipper.AddOpenSubject(test_case.legacy_open_subjects);
    clipper.AddClip(test_case.legacy_clips);
    legacy::Paths64 closed;
    legacy::Paths64 open;
    clipper.Execute(
        oracle::to_legacy_clip_type(test_case.operation), test_case.legacy_fill_rule, closed, open);
    benchmark::DoNotOptimize(closed);
    return open;
}

[[nodiscard]] auto execute_next_open_path_overlay(const open_path_overlay_case& test_case)
    -> next::Paths64 {
    auto result = next::clip(test_case.next_request);
    benchmark::DoNotOptimize(result.closed);
    return std::move(result.open);
}

[[nodiscard]] auto execute_legacy_offset(const offset_case& test_case) -> legacy::Paths64 {
    legacy::ClipperOffset offset;
    offset.PreserveCollinear(test_case.next_request.options.preserve_collinear);
    offset.ReverseSolution(test_case.next_request.options.reverse_solution);
    offset.AddPaths(test_case.legacy_paths,
                    to_legacy_join_type(test_case.next_request.join_type),
                    to_legacy_end_type(test_case.next_request.end_type));
    legacy::Paths64 solution;
    offset.Execute(test_case.next_request.delta, solution);
    return solution;
}

[[nodiscard]] auto execute_next_offset(const offset_case& test_case) -> next::Paths64 {
    return next::offset(test_case.next_request).closed;
}

[[nodiscard]] auto execute_legacy_triangulation(const triangulation_case& test_case)
    -> legacy::Paths64 {
    legacy::Paths64 triangles;
    const auto status =
        legacy::Triangulate(test_case.legacy_paths, triangles, test_case.next_request.use_delaunay);
    if (status != legacy::TriangulateResult::success) { return {}; }
    return triangles;
}

[[nodiscard]] auto execute_next_triangulation(const triangulation_case& test_case)
    -> next::Paths64 {
    const auto result = next::triangulate(test_case.next_request);
    if (result.status != next::TriangulateResult::success) { return {}; }
    return result.triangles;
}

[[nodiscard]] auto execute_legacy_bounds(const bounds_case& test_case) -> legacy::Rect64 {
    return legacy::GetBounds(test_case.legacy_paths);
}

[[nodiscard]] auto execute_next_bounds(const bounds_case& test_case) -> next::Rect64 {
    return next::bounds(test_case.next_paths);
}

[[nodiscard]] auto execute_legacy_minkowski(const minkowski_case& test_case) -> legacy::Paths64 {
    if (test_case.is_difference) {
        return legacy::MinkowskiDiff(
            test_case.legacy_pattern, test_case.legacy_path, test_case.next_request.is_closed);
    }
    return legacy::MinkowskiSum(
        test_case.legacy_pattern, test_case.legacy_path, test_case.next_request.is_closed);
}

[[nodiscard]] auto execute_next_minkowski(const minkowski_case& test_case) -> next::Paths64 {
    if (test_case.is_difference) { return next::minkowski_difference(test_case.next_request); }
    return next::minkowski_sum(test_case.next_request);
}

[[nodiscard]] auto execute_legacy_tree(const tree_case& test_case) -> std::size_t {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(test_case.next_request.options.preserve_collinear);
    clipper.ReverseSolution(test_case.next_request.options.reverse_solution);
    clipper.AddSubject(test_case.legacy_subjects);
    clipper.AddClip(test_case.legacy_clips);
    legacy::PolyTree64 result;
    clipper.Execute(
        oracle::to_legacy_clip_type(test_case.operation), test_case.legacy_fill_rule, result);
    return result.Count();
}

[[nodiscard]] auto execute_next_tree(const tree_case& test_case) -> std::size_t {
    auto result = next::clip_tree(test_case.next_request);
    return result.tree.count(result.tree.root());
}

[[nodiscard]] auto execute_legacy_batch_scalar(const batch_case& test_case) -> std::size_t {
    std::size_t output_paths = 0U;
    for (const auto& request : test_case.scalar_requests) {
        auto result = execute_legacy(request);
        output_paths += result.size();
        benchmark::DoNotOptimize(result);
    }
    return output_paths;
}

[[nodiscard]] auto execute_next_batch_scalar(const batch_case& test_case) -> std::size_t {
    std::size_t output_paths = 0U;
    for (const auto& request : test_case.scalar_requests) {
        auto result = execute_next(request);
        output_paths += result.size();
        benchmark::DoNotOptimize(result);
    }
    return output_paths;
}

[[nodiscard]] auto execute_next_batch(const batch_case& test_case)
    -> std::vector<next::paths64_result> {
    return next::clip_batch(test_case.next_requests);
}

struct legacy_clip_result final {
    legacy::Paths64 closed{};
    legacy::Paths64 open{};
};

[[nodiscard]] auto execute_legacy_full(const parsed_case& test_case) -> legacy_clip_result {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(test_case.next_request.options.preserve_collinear);
    clipper.ReverseSolution(test_case.next_request.options.reverse_solution);
    clipper.AddSubject(test_case.legacy_subjects);
    clipper.AddClip(test_case.legacy_clips);
    legacy_clip_result result;
    if (!clipper.Execute(oracle::to_legacy_clip_type(test_case.operation),
                         to_legacy_fill_rule(test_case.next_request.fill_rule),
                         result.closed,
                         result.open)) {
        throw std::runtime_error{"legacy overlay execution failed"};
    }
    return result;
}

[[nodiscard]] auto execute_legacy_full(const open_path_overlay_case& test_case)
    -> legacy_clip_result {
    legacy::Clipper64 clipper;
    clipper.PreserveCollinear(test_case.next_request.options.preserve_collinear);
    clipper.ReverseSolution(test_case.next_request.options.reverse_solution);
    clipper.AddOpenSubject(test_case.legacy_open_subjects);
    clipper.AddClip(test_case.legacy_clips);
    legacy_clip_result result;
    if (!clipper.Execute(oracle::to_legacy_clip_type(test_case.operation),
                         test_case.legacy_fill_rule,
                         result.closed,
                         result.open)) {
        throw std::runtime_error{"legacy open-path overlay execution failed"};
    }
    return result;
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

#if defined(CLIPPER2NEXT_MSVC_PGO_INSTRUMENTED)
[[nodiscard]] auto load_current_training_profiles() -> current_training_profiles {
    const auto root_value = geometry_corpus_root();
    if (root_value.empty()) {
        throw std::runtime_error{"CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set"};
    }
    const auto root = std::filesystem::path{root_value};
    auto grouped_overlay = load_geometry_corpus_profile_cases(root);
    auto overlay = grouped_overlay.find(std::string{source_name});
    if (overlay == grouped_overlay.end() || overlay->second.empty()) {
        throw std::runtime_error{"benchmark profile is empty: overlay"};
    }

    current_training_profiles profiles;
    profiles.overlay = std::move(overlay->second);
    profiles.rectclip = load_rectclip_cases(root);
    profiles.line_clip = load_line_clip_cases(root);
    profiles.open_path_overlay = load_open_path_overlay_cases(root);
    profiles.offset = load_offset_cases(root);
    profiles.triangulation = load_triangulation_cases(root);
    profiles.bounds = load_bounds_cases(root);
    profiles.minkowski = load_minkowski_cases(root);
    profiles.polytree = load_tree_cases(root, "polytree");
    profiles.clip_tree = load_tree_cases(root, "clip-tree");
    profiles.batch = load_batch_cases(root);
    return profiles;
}

template <typename Cases, typename Function>
auto train_current_cases(const Cases& cases, Function&& function) -> std::size_t {
    if (cases.empty()) { throw std::runtime_error{"PGO training profile is empty"}; }
    constexpr auto minimum_profile_training_time = std::chrono::seconds{2};
    const auto deadline = std::chrono::steady_clock::now() + minimum_profile_training_time;
    std::size_t executions = 0U;
    do {
        for (const auto& test_case : cases) {
            auto result = function(test_case);
            benchmark::DoNotOptimize(result);
        }
        executions += cases.size();
    } while (std::chrono::steady_clock::now() < deadline);
    return executions;
}

[[nodiscard]] auto train_current_benchmark_profiles() -> std::size_t {
    auto profiles = load_current_training_profiles();
    if (!PgoAutoSweep) { throw std::runtime_error{"PgoAutoSweep is unavailable"}; }
    PgoAutoSweep("clipper2next_pgo_fixture_discard");

    std::size_t executions = 0U;
    executions += train_current_cases(profiles.overlay, execute_next);
    executions += train_current_cases(profiles.rectclip, execute_next_rectclip);
    executions += train_current_cases(profiles.line_clip, execute_next_line_clip);
    executions += train_current_cases(profiles.open_path_overlay, execute_next_open_path_overlay);
    executions += train_current_cases(profiles.offset, execute_next_offset);
    executions += train_current_cases(profiles.triangulation, execute_next_triangulation);
    executions += train_current_cases(profiles.bounds, execute_next_bounds);
    executions += train_current_cases(profiles.minkowski, execute_next_minkowski);
    executions += train_current_cases(profiles.polytree, execute_next_tree);
    executions += train_current_cases(profiles.clip_tree, execute_next_tree);
    executions += train_current_cases(profiles.batch, execute_next_batch_scalar);
    executions += train_current_cases(profiles.batch, execute_next_batch);
    return executions;
}
#else
[[nodiscard]] auto train_current_benchmark_profiles() -> std::size_t {
    throw std::runtime_error{"PGO training requires an MSVC instrumented benchmark"};
}
#endif

template <typename Cases, typename Verifier>
[[nodiscard]] auto verify_profile_against_legacy(std::string_view profile,
                                                 const Cases& cases,
                                                 Verifier&& verifier) -> std::size_t {
    if (cases.empty()) {
        throw std::runtime_error{"benchmark profile is empty: " + std::string{profile}};
    }
    for (std::size_t index = 0; index < cases.size(); ++index) {
        try {
            verifier(cases[index]);
        } catch (const std::exception& error) {
            throw std::runtime_error{"legacy equivalence mismatch in profile " +
                                     std::string{profile} +
                                     ", case " + std::to_string(index) + ": " + error.what()};
        }
    }
    return cases.size();
}

[[nodiscard]] auto verify_all_benchmark_profiles_against_legacy() -> std::size_t {
    const auto root_value = geometry_corpus_root();
    if (root_value.empty()) {
        throw std::runtime_error{"CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set"};
    }
    const auto root = std::filesystem::path{root_value};
    std::size_t verified_cases = 0U;

    const auto grouped_overlay_cases = load_geometry_corpus_profile_cases(root);
    const auto overlay = grouped_overlay_cases.find(std::string{source_name});
    if (overlay == grouped_overlay_cases.end()) {
        throw std::runtime_error{"benchmark profile is empty: overlay"};
    }
    verified_cases +=
        verify_profile_against_legacy("overlay", overlay->second, [](const parsed_case& test_case) {
            const auto expected = execute_legacy_full(test_case);
            const auto actual = next::clip(test_case.next_request);
            oracle::assert_paths_semantically_equal(expected.closed, actual.closed);
            oracle::assert_open_paths_exactly_equal(expected.open, actual.open);
        });

    const auto rectclip_cases = load_rectclip_cases(root);
    verified_cases +=
        verify_profile_against_legacy("rectclip", rectclip_cases, [](const rectclip_case& test_case) {
            oracle::assert_paths_semantically_equal(execute_legacy_rectclip(test_case),
                                                    execute_next_rectclip(test_case));
        });

    const auto line_clip_cases = load_line_clip_cases(root);
    verified_cases += verify_profile_against_legacy(
        "rectclip-lines", line_clip_cases, [](const line_clip_case& test_case) {
            oracle::assert_open_paths_exactly_equal(execute_legacy_line_clip(test_case),
                                                    execute_next_line_clip(test_case));
        });

    const auto open_overlay_cases = load_open_path_overlay_cases(root);
    verified_cases += verify_profile_against_legacy(
        "open-path-overlay", open_overlay_cases, [](const open_path_overlay_case& test_case) {
            const auto expected = execute_legacy_full(test_case);
            const auto actual = next::clip(test_case.next_request);
            oracle::assert_paths_semantically_equal(expected.closed, actual.closed);
            oracle::assert_open_paths_exactly_equal(expected.open, actual.open);
        });

    const auto offset_cases = load_offset_cases(root);
    verified_cases += verify_profile_against_legacy(
        "offset", offset_cases, [](const offset_case& test_case) {
            oracle::assert_paths_semantically_equal(execute_legacy_offset(test_case),
                                                    execute_next_offset(test_case));
        });

    const auto triangulation_cases = load_triangulation_cases(root);
    verified_cases += verify_profile_against_legacy(
        "triangulation", triangulation_cases, [](const triangulation_case& test_case) {
            legacy::Paths64 expected_triangles;
            const auto expected_status = legacy::Triangulate(
                test_case.legacy_paths, expected_triangles, test_case.next_request.use_delaunay);
            const auto actual = next::triangulate(test_case.next_request);
            if (to_next_status(expected_status) != actual.status) {
                throw std::runtime_error{"triangulation status mismatch"};
            }
            if (actual.status == next::TriangulateResult::success) {
                oracle::assert_paths_semantically_equal(expected_triangles, actual.triangles);
            } else if (!actual.triangles.empty()) {
                throw std::runtime_error{"failed triangulation returned triangles"};
            }
        });

    const auto bounds_cases = load_bounds_cases(root);
    verified_cases +=
        verify_profile_against_legacy("bounds", bounds_cases, [](const bounds_case& test_case) {
            const auto expected = execute_legacy_bounds(test_case);
            const auto actual = execute_next_bounds(test_case);
            if (expected.left != actual.left || expected.top != actual.top ||
                expected.right != actual.right || expected.bottom != actual.bottom) {
                throw std::runtime_error{"bounds mismatch"};
            }
        });

    const auto minkowski_cases = load_minkowski_cases(root);
    verified_cases +=
        verify_profile_against_legacy("minkowski", minkowski_cases, [](const minkowski_case& test_case) {
            oracle::assert_paths_semantically_equal(execute_legacy_minkowski(test_case),
                                                    execute_next_minkowski(test_case));
        });

    for (const auto profile : {std::string_view{"polytree"}, std::string_view{"clip-tree"}}) {
        const auto tree_cases = load_tree_cases(root, profile);
        verified_cases +=
            verify_profile_against_legacy(profile, tree_cases, [](const tree_case& test_case) {
                legacy::Clipper64 clipper;
                clipper.PreserveCollinear(test_case.next_request.options.preserve_collinear);
                clipper.ReverseSolution(test_case.next_request.options.reverse_solution);
                clipper.AddSubject(test_case.legacy_subjects);
                clipper.AddClip(test_case.legacy_clips);
                legacy::PolyTree64 expected_tree;
                legacy::Paths64 expected_open;
                if (!clipper.Execute(oracle::to_legacy_clip_type(test_case.operation),
                                     test_case.legacy_fill_rule,
                                     expected_tree,
                                     expected_open)) {
                    throw std::runtime_error{"legacy tree execution failed"};
                }
                const auto actual = next::clip_tree(test_case.next_request);
                oracle::assert_open_paths_exactly_equal(expected_open, actual.open);
                oracle::assert_poly_tree_semantically_equal(expected_tree, actual.tree);
            });
    }

    const auto batch_cases = load_batch_cases(root);
    static_cast<void>(verify_profile_against_legacy(
        "batch", batch_cases, [&verified_cases](const batch_case& test_case) {
            const auto batch = execute_next_batch(test_case);
            if (batch.size() != test_case.scalar_requests.size()) {
                throw std::runtime_error{"batch result count mismatch"};
            }
            for (std::size_t index = 0; index < test_case.scalar_requests.size(); ++index) {
                const auto& request = test_case.scalar_requests[index];
                const auto expected = execute_legacy_full(request);
                const auto scalar = next::clip(request.next_request);
                oracle::assert_paths_semantically_equal(expected.closed, scalar.closed);
                oracle::assert_open_paths_exactly_equal(expected.open, scalar.open);
                oracle::assert_paths_semantically_equal(scalar.closed, batch[index].closed);
                oracle::assert_open_paths_exactly_equal(scalar.open, batch[index].open);
                ++verified_cases;
            }
        }));

    return verified_cases;
}

template <typename Case, typename Function>
auto run_profile_cases(benchmark::State& state, const std::vector<Case>& cases, Function&& function)
    -> void {
    for (auto _ : state) {
        for (const auto& test_case : cases) {
            auto result = function(test_case);
            benchmark::DoNotOptimize(result);
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * cases.size()));
}

template <typename Function>
auto run_or_skip(benchmark::State& state, Function&& function) -> void {
    try {
        function();
    } catch (const std::exception& error) { state.SkipWithError(error.what()); } catch (...) {
        state.SkipWithError("unknown external corpus benchmark failure");
    }
}

template <typename Loader, typename Function>
auto run_cases_loaded_on_first_iteration(benchmark::State& state,
                                         Loader&& loader,
                                         Function&& function,
                                         std::string_view empty_message) -> void {
    using cases_type = std::invoke_result_t<Loader&>;
    std::optional<cases_type> cases;
    std::size_t case_count = 0;
    for (auto _ : state) {
        if (!cases.has_value()) {
            state.PauseTiming();
            try {
                cases.emplace(loader());
            } catch (...) {
                state.ResumeTiming();
                throw;
            }
            case_count = cases->size();
            if (cases->empty()) {
                state.ResumeTiming();
                state.SkipWithError(std::string{empty_message}.c_str());
                return;
            }
            state.ResumeTiming();
        }
        for (const auto& test_case : *cases) {
            auto result = function(test_case);
            benchmark::DoNotOptimize(result);
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * case_count));
}

template <typename Loader, typename Function>
auto run_batch_loaded_on_first_iteration(benchmark::State& state,
                                         Loader&& loader,
                                         Function&& function,
                                         std::string_view empty_message) -> void {
    using request_type = std::invoke_result_t<Loader&>;
    std::optional<request_type> requests;
    std::size_t request_count = 0;
    for (auto _ : state) {
        if (!requests.has_value()) {
            state.PauseTiming();
            try {
                requests.emplace(loader());
            } catch (...) {
                state.ResumeTiming();
                throw;
            }
            request_count = requests->size();
            if (requests->empty()) {
                state.ResumeTiming();
                state.SkipWithError(std::string{empty_message}.c_str());
                return;
            }
            state.ResumeTiming();
        }
        auto result = function(*requests);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * request_count));
}

auto register_source(std::string_view source) -> void {
    const auto name = std::string{source};
    const auto run_cases = [name](benchmark::State& state, auto function) {
        run_or_skip(state, [&] {
            run_cases_loaded_on_first_iteration(
                state,
                [&] { return load_source_cases(name); },
                function,
                "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set or source is empty");
        });
    };

    benchmark::RegisterBenchmark(
        ("BM_external_legacy/" + name).c_str(),
        [run_cases](benchmark::State& state) { run_cases(state, execute_legacy); });
    benchmark::RegisterBenchmark(
        ("BM_external_next/" + name).c_str(),
        [run_cases](benchmark::State& state) { run_cases(state, execute_next); });
    benchmark::RegisterBenchmark(
        ("BM_external_next_prepared/" + name).c_str(),
        [run_cases](benchmark::State& state) { run_cases(state, execute_next_prepared); });
    benchmark::RegisterBenchmark(
        ("BM_external_next_batch/" + name).c_str(), [name](benchmark::State& state) {
            run_or_skip(state, [&] {
                run_batch_loaded_on_first_iteration(
                    state,
                    [&] {
                        const auto cases = load_source_cases(name);
                        auto requests = std::vector<next::clip_request64>{};
                        requests.reserve(cases.size());
                        for (const auto& test_case : cases) {
                            requests.push_back(test_case.next_request);
                        }
                        return requests;
                    },
                    [](const std::vector<next::clip_request64>& requests) {
                        return next::clip_batch(requests);
                    },
                    "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set or source is empty");
            });
        });
    benchmark::RegisterBenchmark(
        ("BM_external_next_prepared_batch/" + name).c_str(), [name](benchmark::State& state) {
            run_or_skip(state, [&] {
                run_batch_loaded_on_first_iteration(
                    state,
                    [&] {
                        const auto cases = load_source_cases(name);
                        auto requests = std::vector<next::prepared_clip_request64>{};
                        requests.reserve(cases.size());
                        for (const auto& test_case : cases) {
                            requests.push_back(next::prepare_clip_request(test_case.next_request));
                        }
                        return requests;
                    },
                    [](const std::vector<next::prepared_clip_request64>& requests) {
                        return next::clip_batch(requests);
                    },
                    "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set or source is empty");
            });
        });

    constexpr auto operations = std::array{
        oracle::overlay_operation::intersection,
        oracle::overlay_operation::union_,
        oracle::overlay_operation::difference,
        oracle::overlay_operation::xor_,
    };
    for (const auto operation : operations) {
        const auto suffix = std::string{operation_suffix(operation)};
        const auto load_operation_cases = [name, operation] {
            return filter_cases_by_operation(load_source_cases(name), operation);
        };
        benchmark::RegisterBenchmark(("BM_external_overlay_" + suffix + "_legacy/" + name).c_str(),
                                     [load_operation_cases](benchmark::State& state) {
                                         run_or_skip(state, [&] {
                                             run_cases_loaded_on_first_iteration(
                                                 state,
                                                 load_operation_cases,
                                                 execute_legacy,
                                                 "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set or "
                                                 "operation bucket is empty");
                                         });
                                     });
        benchmark::RegisterBenchmark(("BM_external_overlay_" + suffix + "_next/" + name).c_str(),
                                     [load_operation_cases](benchmark::State& state) {
                                         run_or_skip(state, [&] {
                                             run_cases_loaded_on_first_iteration(
                                                 state,
                                                 load_operation_cases,
                                                 execute_next,
                                                 "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set or "
                                                 "operation bucket is empty");
                                         });
                                     });
    }
}

template <typename Loader, typename Function>
auto run_loaded_profile(benchmark::State& state, Loader&& loader, Function&& function) -> void {
    run_or_skip(state, [&] {
        run_cases_loaded_on_first_iteration(
            state,
            [&] {
                const auto root_value = geometry_corpus_root();
                if (root_value.empty()) {
                    throw std::runtime_error{"CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set"};
                }
                return loader(std::filesystem::path{root_value});
            },
            function,
            "external corpus benchmark profile is empty");
    });
}

template <typename Loader, typename LegacyFunction, typename NextFunction>
auto register_profile_pair(std::string_view profile,
                           Loader loader,
                           LegacyFunction legacy_function,
                           NextFunction next_function) -> void {
    const auto prefix = "BM_external_" + std::string{profile};
    benchmark::RegisterBenchmark((prefix + "_legacy/" + std::string{source_name}).c_str(),
                                 [loader, legacy_function](benchmark::State& state) {
                                     run_loaded_profile(state, loader, legacy_function);
                                 });
    benchmark::RegisterBenchmark((prefix + "_next_unprepared/" + std::string{source_name}).c_str(),
                                 [loader, next_function](benchmark::State& state) {
                                     run_loaded_profile(state, loader, next_function);
                                 });
}

template <typename Loader, typename Function>
auto register_profile(std::string_view benchmark_name, Loader loader, Function function) -> void {
    const auto name = "BM_external_" + std::string{benchmark_name} + "/" + std::string{source_name};
    benchmark::RegisterBenchmark(name.c_str(), [loader, function](benchmark::State& state) {
        run_loaded_profile(state, loader, function);
    });
}

auto register_profile_benchmarks() -> void {
    register_profile_pair(
        "rectclip", load_rectclip_cases, execute_legacy_rectclip, execute_next_rectclip);
    register_profile_pair(
        "open_line_clip", load_line_clip_cases, execute_legacy_line_clip, execute_next_line_clip);
    register_profile_pair("open_path_overlay",
                          load_open_path_overlay_cases,
                          execute_legacy_open_path_overlay,
                          execute_next_open_path_overlay);
    register_profile_pair("offset", load_offset_cases, execute_legacy_offset, execute_next_offset);
    register_profile_pair("triangulation",
                          load_triangulation_cases,
                          execute_legacy_triangulation,
                          execute_next_triangulation);
    register_profile_pair("bounds", load_bounds_cases, execute_legacy_bounds, execute_next_bounds);
    register_profile_pair(
        "minkowski", load_minkowski_cases, execute_legacy_minkowski, execute_next_minkowski);
    register_profile_pair(
        "polytree",
        [](const std::filesystem::path& root) { return load_tree_cases(root, "polytree"); },
        execute_legacy_tree,
        execute_next_tree);
    register_profile_pair(
        "clip_tree",
        [](const std::filesystem::path& root) { return load_tree_cases(root, "clip-tree"); },
        execute_legacy_tree,
        execute_next_tree);
    register_profile_pair(
        "batch_scalar", load_batch_cases, execute_legacy_batch_scalar, execute_next_batch_scalar);
    register_profile("batch_next_batch", load_batch_cases, execute_next_batch);
}

auto BM_external_load_corpus(benchmark::State& state) -> void {
    std::size_t loaded_case_count = 0;
    for (auto _ : state) {
        auto loaded_cases = load_cases();
        loaded_case_count = 0;
        for (const auto& [source, cases] : loaded_cases) {
            benchmark::DoNotOptimize(source.data());
            loaded_case_count += cases.size();
        }
        benchmark::DoNotOptimize(loaded_case_count);
    }
    if (loaded_case_count == 0U) {
        state.SkipWithError("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set or profile is empty");
        return;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * loaded_case_count));
}

BENCHMARK(BM_external_load_corpus);

const auto register_external_benchmarks = [] {
    register_source(source_name);
    register_profile_benchmarks();
    return 0;
}();

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--clipper2next_train_pgo") {
        try {
            const auto executions = train_current_benchmark_profiles();
            std::cout << "MSVC PGO current-path training status=PASS executions=" << executions
                      << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "MSVC PGO current-path training status=FAIL: " << error.what() << '\n';
            return 1;
        }
    }

    if (argc == 2 && std::string_view{argv[1]} == "--clipper2next_verify_legacy") {
        try {
            const auto verified_cases = verify_all_benchmark_profiles_against_legacy();
            std::cout << "optimized legacy equivalence preflight status=PASS profiles=11 cases="
                      << verified_cases << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "optimized legacy equivalence preflight status=FAIL: " << error.what()
                      << '\n';
            return 1;
        }
    }

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) { return 1; }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
