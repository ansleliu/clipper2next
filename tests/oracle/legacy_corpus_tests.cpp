#include <gtest/gtest.h>

#include "geometry_corpus_root.h"
#include "path_equivalence.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

namespace {

struct legacy_corpus_case final {
    int caption = 0;
    legacy::ClipType clip_type = legacy::ClipType::NoClip;
    legacy::FillRule fill_rule = legacy::FillRule::EvenOdd;
    int64_t stored_area = 0;
    int64_t stored_count = 0;
    legacy::Paths64 subjects{};
    legacy::Paths64 subject_open{};
    legacy::Paths64 clips{};
};

template <typename Paths>
struct clip_summary final {
    std::size_t closed_count = 0;
    std::size_t open_count = 0;
    std::size_t open_vertex_count = 0;
    int64_t closed_area = 0;
    Paths closed_paths{};
    Paths open_paths{};
};

struct next_tree_execution final {
    next::PolyTree64 tree{};
    next::Paths64 open{};
};

struct legacy_tree_summary final {
    std::size_t root_count = 0;
    std::size_t node_count = 0;
    std::size_t open_count = 0;
    int64_t area = 0;
};

struct tree_node_signature final {
    bool is_hole = false;
    next::Path64 polygon{};
    std::vector<tree_node_signature> children{};
};

[[nodiscard]] auto read_integer(std::string_view text, std::size_t& offset, int64_t& value)
    -> bool {
    while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t')) { ++offset; }
    if (offset == text.size()) { return false; }

    const auto negative = text[offset] == '-';
    if (negative) { ++offset; }

    const auto digits_begin = offset;
    value = 0;
    while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
        value = (value * 10) + static_cast<int64_t>(text[offset] - '0');
        ++offset;
    }
    if (offset == digits_begin) { return false; }

    while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t')) { ++offset; }
    if (offset < text.size() && text[offset] == ',') { ++offset; }
    if (negative) { value = -value; }
    return true;
}

[[nodiscard]] auto first_integer_after_colon(std::string_view line) -> int64_t {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) { return 0; }

    auto offset = colon + 1U;
    int64_t value = 0;
    return read_integer(line, offset, value) ? value : 0;
}

[[nodiscard]] auto parse_path(std::string_view line, legacy::Path64& path) -> bool {
    std::size_t offset = 0;
    int64_t x = 0;
    int64_t y = 0;
    while (read_integer(line, offset, x) && read_integer(line, offset, y)) {
        path.emplace_back(x, y);
    }
    return !path.empty();
}

auto read_paths(const std::vector<std::string>& lines,
                std::size_t& line_index,
                legacy::Paths64& paths) -> void {
    auto next_line = line_index + 1U;
    while (next_line < lines.size()) {
        legacy::Path64 path;
        if (!parse_path(lines[next_line], path)) { break; }
        paths.push_back(std::move(path));
        ++next_line;
    }
    line_index = next_line - 1U;
}

auto push_case_if_present(std::vector<legacy_corpus_case>& cases,
                          legacy_corpus_case& current,
                          bool has_case) -> void {
    if (has_case) {
        cases.push_back(std::move(current));
        current = legacy_corpus_case{};
    }
}

[[nodiscard]] auto load_legacy_corpus_cases(const std::filesystem::path& corpus_path,
                                            const std::string& label)
    -> std::vector<legacy_corpus_case> {
    std::ifstream input{corpus_path};
    if (!input) {
        throw std::runtime_error{"failed to open " + label + " at " + corpus_path.string()};
    }

    std::vector<std::string> file_lines;
    for (std::string line; std::getline(input, line);) { file_lines.push_back(std::move(line)); }

    std::vector<legacy_corpus_case> cases;
    auto current = legacy_corpus_case{};
    auto has_case = false;

    for (std::size_t line_index = 0; line_index < file_lines.size(); ++line_index) {
        const auto line = std::string_view{file_lines[line_index]};
        if (line.find("CAPTION:") != std::string_view::npos) {
            push_case_if_present(cases, current, has_case);
            has_case = true;
            current.caption = static_cast<int>(first_integer_after_colon(line));
        } else if (line.find("INTERSECTION") != std::string_view::npos) {
            current.clip_type = legacy::ClipType::Intersection;
        } else if (line.find("UNION") != std::string_view::npos) {
            current.clip_type = legacy::ClipType::Union;
        } else if (line.find("DIFFERENCE") != std::string_view::npos) {
            current.clip_type = legacy::ClipType::Difference;
        } else if (line.find("XOR") != std::string_view::npos) {
            current.clip_type = legacy::ClipType::Xor;
        } else if (line.find("EVENODD") != std::string_view::npos) {
            current.fill_rule = legacy::FillRule::EvenOdd;
        } else if (line.find("NONZERO") != std::string_view::npos) {
            current.fill_rule = legacy::FillRule::NonZero;
        } else if (line.find("POSITIVE") != std::string_view::npos) {
            current.fill_rule = legacy::FillRule::Positive;
        } else if (line.find("NEGATIVE") != std::string_view::npos) {
            current.fill_rule = legacy::FillRule::Negative;
        } else if (line.find("SOL_AREA") != std::string_view::npos) {
            current.stored_area = first_integer_after_colon(line);
        } else if (line.find("SOL_COUNT") != std::string_view::npos) {
            current.stored_count = first_integer_after_colon(line);
        } else if (line.find("SUBJECTS_OPEN") != std::string_view::npos) {
            read_paths(file_lines, line_index, current.subject_open);
        } else if (line.find("SUBJECTS") != std::string_view::npos) {
            read_paths(file_lines, line_index, current.subjects);
        } else if (line.find("CLIPS") != std::string_view::npos) {
            read_paths(file_lines, line_index, current.clips);
        }
    }

    push_case_if_present(cases, current, has_case);
    return cases;
}

[[nodiscard]] auto to_next_clip_type(legacy::ClipType value) -> next::ClipType {
    switch (value) {
    case legacy::ClipType::NoClip: {
        return next::ClipType::NoClip;
    }
    case legacy::ClipType::Intersection: {
        return next::ClipType::Intersection;
    }
    case legacy::ClipType::Union: {
        return next::ClipType::Union;
    }
    case legacy::ClipType::Difference: {
        return next::ClipType::Difference;
    }
    case legacy::ClipType::Xor: {
        return next::ClipType::Xor;
    }
    }
    return next::ClipType::NoClip;
}

[[nodiscard]] auto to_next_fill_rule(legacy::FillRule value) -> next::FillRule {
    switch (value) {
    case legacy::FillRule::EvenOdd: {
        return next::FillRule::EvenOdd;
    }
    case legacy::FillRule::NonZero: {
        return next::FillRule::NonZero;
    }
    case legacy::FillRule::Positive: {
        return next::FillRule::Positive;
    }
    case legacy::FillRule::Negative: {
        return next::FillRule::Negative;
    }
    }
    return next::FillRule::EvenOdd;
}

[[nodiscard]] auto to_next_path(const legacy::Path64& path) -> next::Path64 {
    next::Path64 result;
    result.reserve(path.size());
    for (const auto& point : path) { result.push_back(next::Point64{point.x, point.y}); }
    return result;
}

[[nodiscard]] auto to_next_paths(const legacy::Paths64& paths) -> next::Paths64 {
    next::Paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) { result.push_back(to_next_path(path)); }
    return result;
}

[[nodiscard]] auto oriented_legacy_rectangle(
    int64_t left, int64_t top, int64_t right, int64_t bottom, bool positive) -> legacy::Path64 {
    legacy::Path64 path{{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    if ((legacy::Area(path) > 0.0) != positive) { std::reverse(path.begin(), path.end()); }
    return path;
}

[[nodiscard]] auto make_generated_polytree_case(int index) -> legacy_corpus_case {
    const auto shift = static_cast<int64_t>(index * 600);
    legacy_corpus_case test_case;
    test_case.caption = 10'000 + index;
    test_case.clip_type = legacy::ClipType::Union;
    test_case.fill_rule = legacy::FillRule::NonZero;
    test_case.subjects =
        legacy::Paths64{oriented_legacy_rectangle(shift, 0, shift + 500, 500, true),
                        oriented_legacy_rectangle(shift + 60, 60, shift + 440, 440, false),
                        oriented_legacy_rectangle(shift + 130, 130, shift + 370, 370, true),
                        oriented_legacy_rectangle(shift + 190, 190, shift + 310, 310, false)};
    return test_case;
}

[[nodiscard]] auto summarize_open_vertices(const legacy::Paths64& paths) -> std::size_t {
    std::size_t count = 0;
    for (const auto& path : paths) { count += path.size(); }
    return count;
}

[[nodiscard]] auto summarize_open_vertices(const next::Paths64& paths) -> std::size_t {
    std::size_t count = 0;
    for (const auto& path : paths) { count += path.size(); }
    return count;
}

[[nodiscard]] auto execute_legacy_clip(const legacy_corpus_case& test_case)
    -> clip_summary<legacy::Paths64> {
    legacy::Clipper64 clipper;
    clipper.AddSubject(test_case.subjects);
    clipper.AddOpenSubject(test_case.subject_open);
    clipper.AddClip(test_case.clips);

    legacy::Paths64 closed_solution;
    legacy::Paths64 open_solution;
    clipper.Execute(test_case.clip_type, test_case.fill_rule, closed_solution, open_solution);
    return clip_summary<legacy::Paths64>{
        .closed_count = closed_solution.size(),
        .open_count = open_solution.size(),
        .open_vertex_count = summarize_open_vertices(open_solution),
        .closed_area = static_cast<int64_t>(legacy::Area(closed_solution)),
        .closed_paths = std::move(closed_solution),
        .open_paths = std::move(open_solution),
    };
}

[[nodiscard]] auto execute_next_clip(const legacy_corpus_case& test_case)
    -> clip_summary<next::Paths64> {
    next::clip_request64 request;
    request.clip_type = to_next_clip_type(test_case.clip_type);
    request.fill_rule = to_next_fill_rule(test_case.fill_rule);
    request.subjects = to_next_paths(test_case.subjects);
    request.open_subjects = to_next_paths(test_case.subject_open);
    request.clips = to_next_paths(test_case.clips);
    auto solution = next::clip(request);
    const auto closed_area = static_cast<int64_t>(next::area(solution.closed));
    return clip_summary<next::Paths64>{
        .closed_count = solution.closed.size(),
        .open_count = solution.open.size(),
        .open_vertex_count = summarize_open_vertices(solution.open),
        .closed_area = closed_area,
        .closed_paths = std::move(solution.closed),
        .open_paths = std::move(solution.open),
    };
}

[[nodiscard]] auto execute_legacy_clip_tree_summary(const legacy_corpus_case& test_case)
    -> legacy_tree_summary {
    legacy::Clipper64 clipper;
    clipper.AddSubject(test_case.subjects);
    clipper.AddOpenSubject(test_case.subject_open);
    clipper.AddClip(test_case.clips);

    legacy::PolyTree64 tree;
    legacy::Paths64 open;
    clipper.Execute(test_case.clip_type, test_case.fill_rule, tree, open);
    const auto paths = legacy::PolyTreeToPaths64(tree);
    return legacy_tree_summary{
        tree.Count(), paths.size(), open.size(), static_cast<int64_t>(tree.Area())};
}

auto execute_legacy_clip_tree(const legacy_corpus_case& test_case,
                              legacy::PolyTree64& tree,
                              legacy::Paths64& open) -> void {
    legacy::Clipper64 clipper;
    clipper.AddSubject(test_case.subjects);
    clipper.AddOpenSubject(test_case.subject_open);
    clipper.AddClip(test_case.clips);
    clipper.Execute(test_case.clip_type, test_case.fill_rule, tree, open);
}

[[nodiscard]] auto execute_next_clip_tree(const legacy_corpus_case& test_case)
    -> next_tree_execution {
    next::clip_request64 request;
    request.clip_type = to_next_clip_type(test_case.clip_type);
    request.fill_rule = to_next_fill_rule(test_case.fill_rule);
    request.subjects = to_next_paths(test_case.subjects);
    request.open_subjects = to_next_paths(test_case.subject_open);
    request.clips = to_next_paths(test_case.clips);

    const auto result = next::clip_tree(request);
    return next_tree_execution{result.tree, result.open};
}

[[nodiscard]] auto tree_signature_less(const tree_node_signature& lhs,
                                       const tree_node_signature& rhs) -> bool {
    if (lhs.is_hole != rhs.is_hole) { return lhs.is_hole < rhs.is_hole; }
    if (oracle::path_less(lhs.polygon, rhs.polygon)) { return true; }
    if (oracle::path_less(rhs.polygon, lhs.polygon)) { return false; }
    return std::lexicographical_compare(lhs.children.begin(),
                                        lhs.children.end(),
                                        rhs.children.begin(),
                                        rhs.children.end(),
                                        tree_signature_less);
}

[[nodiscard]] auto canonical_tree_signature(tree_node_signature signature) -> tree_node_signature {
    for (auto& child : signature.children) { child = canonical_tree_signature(std::move(child)); }
    std::sort(signature.children.begin(), signature.children.end(), tree_signature_less);
    return signature;
}

[[nodiscard]] auto make_legacy_tree_signature(const legacy::PolyPath64& node)
    -> tree_node_signature {
    tree_node_signature signature;
    signature.is_hole = node.IsHole();
    signature.polygon = oracle::canonical_closed_path(oracle::to_next_path(node.Polygon()));
    signature.children.reserve(node.Count());
    for (std::size_t index = 0; index < node.Count(); ++index) {
        signature.children.push_back(make_legacy_tree_signature(*node.Child(index)));
    }
    return canonical_tree_signature(std::move(signature));
}

[[nodiscard]] auto make_next_tree_signature(const next::PolyTree64& tree,
                                            next::PolyTree64::node_id node) -> tree_node_signature {
    tree_node_signature signature;
    signature.is_hole = tree.is_hole(node);
    signature.polygon = oracle::canonical_closed_path(tree.polygon(node));
    signature.children.reserve(tree.count(node));
    for (const auto child : tree.children(node)) {
        signature.children.push_back(make_next_tree_signature(tree, child));
    }
    return canonical_tree_signature(std::move(signature));
}

[[nodiscard]] auto tree_signatures_equal(const tree_node_signature& lhs,
                                         const tree_node_signature& rhs) -> bool {
    if (lhs.is_hole != rhs.is_hole || lhs.polygon != rhs.polygon ||
        lhs.children.size() != rhs.children.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.children.size(); ++index) {
        if (!tree_signatures_equal(lhs.children[index], rhs.children[index])) { return false; }
    }
    return true;
}

[[nodiscard]] auto execute_legacy_round_offset(const legacy::Paths64& subjects) -> legacy::Paths64 {
    legacy::ClipperOffset offset;
    offset.AddPaths(subjects, legacy::JoinType::Round, legacy::EndType::Polygon);

    legacy::Paths64 solution;
    offset.Execute(1.0, solution);
    return solution;
}

[[nodiscard]] auto execute_next_round_offset(const legacy::Paths64& subjects,
                                             double arc_tolerance = 0.0) -> next::Paths64 {
    next::offset_request64 request;
    request.paths = to_next_paths(subjects);
    request.delta = 1.0;
    request.join_type = next::JoinType::Round;
    request.end_type = next::EndType::Polygon;
    request.arc_tolerance = arc_tolerance;
    return next::offset(request).closed;
}

[[nodiscard]] auto next_tree_node_count(const next::PolyTree64& tree,
                                        next::PolyTree64::node_id parent) -> std::size_t {
    std::size_t count = 0;
    for (const auto child : tree.children(parent)) {
        ++count;
        count += next_tree_node_count(tree, child);
    }
    return count;
}

[[nodiscard]] auto next_path_contains_path(const next::Path64& outer, const next::Path64& inner)
    -> bool {
    for (const auto& point : inner) {
        if (next::point_in_polygon(point, outer) == next::PointInPolygonResult::IsOutside) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto next_tree_fully_contains_children(const next::PolyTree64& tree,
                                                     next::PolyTree64::node_id parent) -> bool {
    const auto& parent_polygon = tree.polygon(parent);
    for (const auto child : tree.children(parent)) {
        if (!parent_polygon.empty() && !next_tree_fully_contains_children(tree, child)) {
            return false;
        }
        if (!parent_polygon.empty() && !tree.polygon(child).empty() &&
            !next_path_contains_path(parent_polygon, tree.polygon(child))) {
            return false;
        }
        if (parent_polygon.empty() && !next_tree_fully_contains_children(tree, child)) {
            return false;
        }
    }
    return true;
}

auto next_tree_point_counter(const next::PolyTree64& tree,
                             next::PolyTree64::node_id parent,
                             const next::Point64& point,
                             int& counter) -> void {
    const auto& polygon = tree.polygon(parent);
    if (!polygon.empty() &&
        next::point_in_polygon(point, polygon) != next::PointInPolygonResult::IsOutside) {
        counter += tree.is_hole(parent) ? -1 : 1;
    }
    for (const auto child : tree.children(parent)) {
        next_tree_point_counter(tree, child, point, counter);
    }
}

[[nodiscard]] auto next_tree_contains_point(const next::PolyTree64& tree,
                                            const next::Point64& point) -> bool {
    int counter = 0;
    for (const auto child : tree.children(tree.root())) {
        next_tree_point_counter(tree, child, point, counter);
    }
    return counter != 0;
}

[[nodiscard]] auto external_legacy_tests_dir() -> std::optional<std::filesystem::path> {
    const auto root = oracle::geometry_corpus_root();
    if (root.empty()) { return std::nullopt; }

    const auto source_root = std::filesystem::path{root} / "sources" / "clipper2-rust" / "Tests";
    const auto preferred = source_root / "data";
    if (std::filesystem::exists(preferred)) { return preferred; }
    return source_root;
}

class Clipper2NextLegacyCorpusTests : public ::testing::Test {
protected:
    auto SetUp() -> void override {
        const auto external_dir = external_legacy_tests_dir();
        if (!external_dir.has_value()) {
            GTEST_SKIP() << "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT is not set";
        }
        legacy_dir_ = *external_dir;
        ASSERT_TRUE(std::filesystem::exists(legacy_dir_)) << legacy_dir_.string();
    }

    std::filesystem::path legacy_dir_{};
};

}  // namespace

TEST_F(Clipper2NextLegacyCorpusTests, LoadsClosedPolygonCorpus) {
    const auto cases =
        load_legacy_corpus_cases(legacy_dir_ / "Polygons.txt", "Polygons.txt");

    EXPECT_GE(cases.size(), static_cast<std::size_t>(195));
}

TEST_F(Clipper2NextLegacyCorpusTests, ClosedPolygonCorpusDoesNotCrash) {
    const auto cases =
        load_legacy_corpus_cases(legacy_dir_ / "Polygons.txt", "Polygons.txt");
    ASSERT_GE(cases.size(), static_cast<std::size_t>(195));

    for (const auto& test_case : cases) {
        SCOPED_TRACE("legacy Polygons.txt caption " + std::to_string(test_case.caption));
        EXPECT_EXIT(
            {
                (void)execute_next_clip(test_case);
                std::exit(0);
            },
            ::testing::ExitedWithCode(0),
            "");
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, ClosedPolygonCorpusMatchesLegacyExactly) {
    const auto cases =
        load_legacy_corpus_cases(legacy_dir_ / "Polygons.txt", "Polygons.txt");
    ASSERT_GE(cases.size(), static_cast<std::size_t>(195));

    for (const auto& test_case : cases) {
        SCOPED_TRACE("legacy Polygons.txt caption " + std::to_string(test_case.caption));
        ASSERT_TRUE(test_case.subject_open.empty());

        const auto legacy_solution = execute_legacy_clip(test_case);
        const auto next_solution = execute_next_clip(test_case);

        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(
            legacy_solution.closed_paths, next_solution.closed_paths));
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, LoadsOpenLineCorpus) {
    const auto cases = load_legacy_corpus_cases(legacy_dir_ / "Lines.txt", "Lines.txt");

    EXPECT_GE(cases.size(), static_cast<std::size_t>(16));
}

TEST_F(Clipper2NextLegacyCorpusTests, OpenLineCorpusDoesNotCrash) {
    const auto cases = load_legacy_corpus_cases(legacy_dir_ / "Lines.txt", "Lines.txt");
    ASSERT_GE(cases.size(), static_cast<std::size_t>(16));

    for (const auto& test_case : cases) {
        SCOPED_TRACE("legacy Lines.txt caption " + std::to_string(test_case.caption));
        EXPECT_EXIT(
            {
                (void)execute_next_clip(test_case);
                std::exit(0);
            },
            ::testing::ExitedWithCode(0),
            "");
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, OpenLineCorpusMatchesLegacyExactly) {
    const auto cases = load_legacy_corpus_cases(legacy_dir_ / "Lines.txt", "Lines.txt");
    ASSERT_GE(cases.size(), static_cast<std::size_t>(16));

    for (const auto& test_case : cases) {
        SCOPED_TRACE("legacy Lines.txt caption " + std::to_string(test_case.caption));

        const auto legacy_solution = execute_legacy_clip(test_case);
        const auto next_solution = execute_next_clip(test_case);

        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(
            legacy_solution.closed_paths, next_solution.closed_paths));
        EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(
            legacy_solution.open_paths, next_solution.open_paths));
        if (test_case.caption == 1) {
            ASSERT_EQ(next_solution.open_paths.size(), 1U);
            ASSERT_EQ(next_solution.open_paths[0].size(), 2U);
            EXPECT_EQ(next_solution.open_paths[0][0].y, 6);
        }
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, LoadsPolyTreeOwnerCorpus) {
    const auto first = load_legacy_corpus_cases(legacy_dir_ / "PolytreeHoleOwner.txt",
                                                "PolytreeHoleOwner.txt");
    const auto second = load_legacy_corpus_cases(legacy_dir_ / "PolytreeHoleOwner2.txt",
                                                 "PolytreeHoleOwner2.txt");

    EXPECT_GE(first.size(), static_cast<std::size_t>(1));
    EXPECT_GE(second.size(), static_cast<std::size_t>(1));
}

TEST_F(Clipper2NextLegacyCorpusTests, PolyTreeOwnerCorpusMatchesLegacySummary) {
    const auto first = load_legacy_corpus_cases(legacy_dir_ / "PolytreeHoleOwner.txt",
                                                "PolytreeHoleOwner.txt");
    const auto second = load_legacy_corpus_cases(legacy_dir_ / "PolytreeHoleOwner2.txt",
                                                 "PolytreeHoleOwner2.txt");
    ASSERT_GE(first.size(), static_cast<std::size_t>(1));
    ASSERT_GE(second.size(), static_cast<std::size_t>(1));

    for (const auto& test_case : {first.front(), second.front()}) {
        SCOPED_TRACE("legacy PolyTree owner corpus caption " + std::to_string(test_case.caption));

        const auto legacy_solution = execute_legacy_clip_tree_summary(test_case);
        const auto next_solution = execute_next_clip_tree(test_case);

        EXPECT_EQ(next_solution.tree.count(), legacy_solution.root_count);
        EXPECT_EQ(next_tree_node_count(next_solution.tree, next_solution.tree.root()),
                  legacy_solution.node_count);
        EXPECT_EQ(next_solution.open.size(), legacy_solution.open_count);
        EXPECT_EQ(static_cast<int64_t>(next_solution.tree.area()), legacy_solution.area);
        EXPECT_TRUE(
            next_tree_fully_contains_children(next_solution.tree, next_solution.tree.root()));
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, PolyTreeOwnerCorpusMatchesLegacyTreeShape) {
    const auto first = load_legacy_corpus_cases(legacy_dir_ / "PolytreeHoleOwner.txt",
                                                "PolytreeHoleOwner.txt");
    const auto second = load_legacy_corpus_cases(legacy_dir_ / "PolytreeHoleOwner2.txt",
                                                 "PolytreeHoleOwner2.txt");
    ASSERT_GE(first.size(), static_cast<std::size_t>(1));
    ASSERT_GE(second.size(), static_cast<std::size_t>(1));

    for (const auto& test_case : {first.front(), second.front()}) {
        SCOPED_TRACE("legacy PolyTree full shape corpus caption " +
                     std::to_string(test_case.caption));

        legacy::PolyTree64 legacy_tree;
        legacy::Paths64 legacy_open;
        execute_legacy_clip_tree(test_case, legacy_tree, legacy_open);
        const auto next_solution = execute_next_clip_tree(test_case);

        EXPECT_EQ(next_solution.open.size(), legacy_open.size());
        const auto expected_signature = make_legacy_tree_signature(legacy_tree);
        const auto actual_signature =
            make_next_tree_signature(next_solution.tree, next_solution.tree.root());
        EXPECT_TRUE(tree_signatures_equal(expected_signature, actual_signature));
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, GeneratedNestedPolyTreeCorpusMatchesLegacyTreeShape) {
    for (int index = 0; index < 12; ++index) {
        const auto test_case = make_generated_polytree_case(index);
        SCOPED_TRACE("generated nested PolyTree case " + std::to_string(test_case.caption));

        legacy::PolyTree64 legacy_tree;
        legacy::Paths64 legacy_open;
        execute_legacy_clip_tree(test_case, legacy_tree, legacy_open);
        const auto next_solution = execute_next_clip_tree(test_case);

        EXPECT_EQ(next_solution.open.size(), legacy_open.size());
        EXPECT_EQ(next_tree_node_count(next_solution.tree, next_solution.tree.root()),
                  legacy::PolyTreeToPaths64(legacy_tree).size());
        const auto expected_signature = make_legacy_tree_signature(legacy_tree);
        const auto actual_signature =
            make_next_tree_signature(next_solution.tree, next_solution.tree.root());
        EXPECT_TRUE(tree_signatures_equal(expected_signature, actual_signature));
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, PolyTreeOwner2CorpusPreservesKnownPointOwnership) {
    const auto cases = load_legacy_corpus_cases(legacy_dir_ / "PolytreeHoleOwner2.txt",
                                                "PolytreeHoleOwner2.txt");
    ASSERT_GE(cases.size(), static_cast<std::size_t>(1));

    const auto solution = execute_next_clip_tree(cases.front());
    ASSERT_GT(next_tree_node_count(solution.tree, solution.tree.root()), 0U);

    const next::Path64 outside_points = {
        {21887, 10420},
        {21726, 10825},
        {21662, 10845},
        {21617, 10890},
    };
    for (const auto& point : outside_points) {
        EXPECT_FALSE(next_tree_contains_point(solution.tree, point));
    }

    const next::Path64 inside_points = {
        {21887, 10430},
        {21843, 10520},
        {21810, 10686},
        {21900, 10461},
    };
    for (const auto& point : inside_points) {
        EXPECT_TRUE(next_tree_contains_point(solution.tree, point));
    }
}

TEST_F(Clipper2NextLegacyCorpusTests, LoadsOffsetCorpus) {
    const auto cases = load_legacy_corpus_cases(legacy_dir_ / "Offsets.txt", "Offsets.txt");

    EXPECT_GE(cases.size(), static_cast<std::size_t>(2));
}

TEST_F(Clipper2NextLegacyCorpusTests, OffsetCorpusMatchesLegacyExactly) {
    const auto cases = load_legacy_corpus_cases(legacy_dir_ / "Offsets.txt", "Offsets.txt");
    ASSERT_GE(cases.size(), static_cast<std::size_t>(2));

    for (const auto& test_case : cases) {
        SCOPED_TRACE("legacy Offsets.txt caption " + std::to_string(test_case.caption));

        const auto legacy_solution = execute_legacy_round_offset(test_case.subjects);
        const auto next_solution = execute_next_round_offset(test_case.subjects);

        EXPECT_NO_THROW(
            oracle::assert_paths_semantically_equal(legacy_solution, next_solution));
    }
}
