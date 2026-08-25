#include <gtest/gtest.h>

#include "path_equivalence.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"
#include "../../benchmarks/oracle/benchmark_fixtures.h"

#include <string>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

namespace {

[[nodiscard]] auto to_legacy_join(next::JoinType join_type) -> legacy::JoinType {
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

[[nodiscard]] auto to_legacy_end(next::EndType end_type) -> legacy::EndType {
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

[[nodiscard]] auto execute_legacy_offset(const next::Paths64& paths,
                                         double delta,
                                         next::JoinType join_type,
                                         next::EndType end_type,
                                         double arc_tolerance = 0.0) -> legacy::Paths64 {
    return legacy::InflatePaths(
        oracle::to_legacy_paths(paths),
        delta,
        to_legacy_join(join_type),
        to_legacy_end(end_type),
        2.0,
        arc_tolerance);
}

[[nodiscard]] auto execute_next_offset(const next::Paths64& paths,
                                       double delta,
                                       next::JoinType join_type,
                                       next::EndType end_type,
                                       double arc_tolerance = 0.0) -> next::Paths64 {
    next::offset_request64 request;
    request.paths = paths;
    request.delta = delta;
    request.join_type = join_type;
    request.end_type = end_type;
    request.arc_tolerance = arc_tolerance;
    return next::offset(request).closed;
}

[[nodiscard]] auto materialize(const next::path_set64& paths) -> next::Paths64 {
    auto result = next::Paths64{};
    result.reserve(paths.size());
    for (const auto path : paths) {
        result.emplace_back(path.begin(), path.end());
    }
    return result;
}

[[nodiscard]] auto matrix_polygon_subject() -> next::Paths64 {
    return next::Paths64{
        {{0, 0}, {180, 0}, {210, 60}, {130, 80}, {130, 150}, {20, 150}, {20, 80}, {-30, 50}}};
}

[[nodiscard]] auto matrix_open_subject() -> next::Paths64 {
    return next::Paths64{{{0, 0}, {70, 20}, {120, -30}, {170, 40}, {240, 15}, {300, 90}}};
}

[[nodiscard]] auto generated_offset_subjects() -> next::Paths64 {
    next::Paths64 subjects;
    subjects.reserve(48U);
    for (int index = 0; index < 48; ++index) {
        const auto x = static_cast<int64_t>((index % 12) * 1000);
        const auto y = static_cast<int64_t>((index / 12) * 900);
        const auto w = static_cast<int64_t>(80 + ((index * 17) % 90));
        const auto h = static_cast<int64_t>(90 + ((index * 11) % 80));
        const auto notch = static_cast<int64_t>(18 + ((index * 7) % 24));
        subjects.push_back(next::Path64{{x, y},
                                        {x + w, y},
                                        {x + w, y + h / 2},
                                        {x + w - notch, y + h / 2},
                                        {x + w - notch, y + h},
                                        {x, y + h}});
    }
    return subjects;
}

[[nodiscard]] auto total_point_count(const next::Paths64& paths) -> std::size_t {
    std::size_t count = 0;
    for (const auto& path : paths) { count += path.size(); }
    return count;
}

auto collect_tree_paths(const next::PolyTree64& tree,
                        next::PolyTree64::node_id id,
                        next::Paths64& paths) -> void {
    if (id != tree.root()) { paths.emplace_back(tree.polygon(id)); }
    for (const auto child : tree.children(id)) { collect_tree_paths(tree, child, paths); }
}

[[nodiscard]] auto collect_tree_paths(const next::PolyTree64& tree) -> next::Paths64 {
    next::Paths64 paths;
    collect_tree_paths(tree, tree.root(), paths);
    return paths;
}

}  // namespace

TEST(Clipper2NextDifferentialOffsetTests, PositiveSquareOffsetMatchesLegacy) {
    const auto legacy_subject = legacy::Paths64{
        legacy::MakePath({0, 0, 100, 0, 100, 100, 0, 100}),
    };
    const auto next_subject = next::Paths64{
        {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };

    const auto expected = legacy::InflatePaths(
        legacy_subject, 10.0, legacy::JoinType::Miter, legacy::EndType::Polygon);
    const auto actual =
        execute_next_offset(next_subject, 10.0, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialOffsetTests, BenchmarkFixtureMiterOffsetMatchesLegacy) {
    const auto legacy_subject = legacy::Paths64{
        legacy::MakePath({0, 0, 500, 0, 650, 300, 300, 650, 0, 500}),
    };
    const auto next_subject = next::Paths64{
        {{0, 0}, {500, 0}, {650, 300}, {300, 650}, {0, 500}},
    };

    const auto expected = legacy::InflatePaths(
        legacy_subject, 25.0, legacy::JoinType::Miter, legacy::EndType::Polygon);
    const auto actual =
        execute_next_offset(next_subject, 25.0, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialOffsetTests, PolygonJoinTypeMatrixMatchesLegacy) {
    const auto subject = matrix_polygon_subject();
    const next::JoinType join_types[] = {next::JoinType::Miter,
                                         next::JoinType::Square,
                                         next::JoinType::Bevel,
                                         next::JoinType::Round};
    const double deltas[] = {12.0, -6.0};

    for (const auto join_type : join_types) {
        for (const auto delta : deltas) {
            SCOPED_TRACE("join=" + std::to_string(static_cast<int>(join_type)) +
                         " delta=" + std::to_string(delta));
            const auto expected =
                execute_legacy_offset(subject, delta, join_type, next::EndType::Polygon);
            const auto actual =
                execute_next_offset(subject, delta, join_type, next::EndType::Polygon);
            EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
        }
    }
}

TEST(Clipper2NextDifferentialOffsetTests, OpenJoinAndEndTypeMatrixMatchesLegacy) {
    const auto subject = matrix_open_subject();
    const next::JoinType join_types[] = {next::JoinType::Miter, next::JoinType::Round};
    const next::EndType end_types[] = {
        next::EndType::Joined,
        next::EndType::Butt,
        next::EndType::Square,
        next::EndType::Round};

    for (const auto join_type : join_types) {
        for (const auto end_type : end_types) {
            SCOPED_TRACE("join=" + std::to_string(static_cast<int>(join_type)) +
                         " end=" + std::to_string(static_cast<int>(end_type)));
            const auto expected = execute_legacy_offset(subject, 9.0, join_type, end_type);
            const auto actual = execute_next_offset(subject, 9.0, join_type, end_type);
            EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
        }
    }
}

TEST(Clipper2NextDifferentialOffsetTests, RoundArcToleranceMatrixMatchesLegacy) {
    const auto polygon = matrix_polygon_subject();
    const auto open = matrix_open_subject();
    for (const auto arc_tolerance : {0.0, 0.01, 0.25, 2.0, 100.0}) {
        SCOPED_TRACE("arc_tolerance=" + std::to_string(arc_tolerance));
        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(
            execute_legacy_offset(polygon,
                                  12.0,
                                  next::JoinType::Round,
                                  next::EndType::Polygon,
                                  arc_tolerance),
            execute_next_offset(polygon,
                                12.0,
                                next::JoinType::Round,
                                next::EndType::Polygon,
                                arc_tolerance)));
        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(
            execute_legacy_offset(open,
                                  9.0,
                                  next::JoinType::Round,
                                  next::EndType::Round,
                                  arc_tolerance),
            execute_next_offset(open,
                                9.0,
                                next::JoinType::Round,
                                next::EndType::Round,
                                arc_tolerance)));
    }
}

TEST(Clipper2NextDifferentialOffsetTests,
     FlatPathSetInputAndOutputMatchLegacyWithoutTolerance) {
    const auto subject = generated_offset_subjects();
    auto flat = next::path_set64{};
    flat.reserve(subject.size(), total_point_count(subject));
    for (const auto& path : subject) {
        flat.append(path, geotypes::PathClosure::ClosedImplicit);
    }

    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(flat.view());
    request.delta = 12.0;
    request.join_type = next::JoinType::Round;
    request.end_type = next::EndType::Polygon;
    request.arc_tolerance = 0.25;
    const auto actual = next::offset_stage_checked(request);

    ASSERT_TRUE(actual.has_value()) << static_cast<int>(actual.error());
    const auto expected = execute_legacy_offset(subject,
                                                request.delta,
                                                request.join_type,
                                                request.end_type,
                                                request.arc_tolerance);
    EXPECT_NO_THROW(
        oracle::assert_paths_semantically_equal(expected, materialize(actual->paths)));
}

TEST(Clipper2NextDifferentialOffsetTests, GeneratedLongPolygonOffsetCorpusMatchesLegacy) {
    const auto subject = generated_offset_subjects();
    const next::JoinType join_types[] = {
        next::JoinType::Miter, next::JoinType::Square, next::JoinType::Bevel};

    for (const auto join_type : join_types) {
        SCOPED_TRACE("join=" + std::to_string(static_cast<int>(join_type)));
        const auto expected =
            execute_legacy_offset(subject, 14.0, join_type, next::EndType::Polygon);
        const auto actual = execute_next_offset(subject, 14.0, join_type, next::EndType::Polygon);
        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
    }
}

TEST(Clipper2NextDifferentialOffsetTests, ManySeparatedPolygonOffsetsMatchLegacy) {
    legacy::Paths64 legacy_subjects;
    next::Paths64 next_subjects;
    for (int index = 0; index < 24; ++index) {
        const auto left = static_cast<int64_t>(index * 1000);
        constexpr int64_t zero = 0;
        constexpr int64_t side = 100;
        legacy::Path64 legacy_path;
        legacy_path.emplace_back(left, zero);
        legacy_path.emplace_back(left + side, zero);
        legacy_path.emplace_back(left + side, side);
        legacy_path.emplace_back(left, side);
        legacy_subjects.push_back(std::move(legacy_path));
        next_subjects.push_back({
            {left, zero},
            {left + side, zero},
            {left + side, side},
            {left, side},
        });
    }

    const auto expected = legacy::InflatePaths(
        legacy_subjects, 25.0, legacy::JoinType::Miter, legacy::EndType::Polygon);
    const auto actual =
        execute_next_offset(next_subjects, 25.0, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialOffsetTests, PositiveConcaveSimpleOffsetMatchesLegacy) {
    const auto legacy_subject = legacy::Paths64{
        legacy::MakePath({0, 0, 120, 0, 120, 40, 40, 40, 40, 120, 0, 120}),
    };
    const auto next_subject = next::Paths64{
        {{0, 0}, {120, 0}, {120, 40}, {40, 40}, {40, 120}, {0, 120}},
    };

    const auto expected = legacy::InflatePaths(
        legacy_subject, 10.0, legacy::JoinType::Miter, legacy::EndType::Polygon);
    const auto actual =
        execute_next_offset(next_subject, 10.0, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialOffsetTests, SeparatedConcavePolygonOffsetsMatchLegacy) {
    const auto first = legacy::MakePath({0, 0, 120, 0, 120, 40, 40, 40, 40, 120, 0, 120});
    const auto second =
        legacy::MakePath({1000, 0, 1120, 0, 1120, 40, 1040, 40, 1040, 120, 1000, 120});
    const auto legacy_subject = legacy::Paths64{first, second};
    const auto next_subject = next::Paths64{
        {{0, 0}, {120, 0}, {120, 40}, {40, 40}, {40, 120}, {0, 120}},
        {{1000, 0}, {1120, 0}, {1120, 40}, {1040, 40}, {1040, 120}, {1000, 120}},
    };

    const auto expected = legacy::InflatePaths(
        legacy_subject, 10.0, legacy::JoinType::Miter, legacy::EndType::Polygon);
    const auto actual =
        execute_next_offset(next_subject, 10.0, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialOffsetTests, BenchmarkFixtureAssertHelperAcceptsNextOffset) {
    const auto fixture = clipper2next::benchmarks::make_offset_fixture();

    const auto expected = clipper2next::benchmarks::legacy::InflatePaths(
        fixture.legacy_subject,
        25.0,
        clipper2next::benchmarks::legacy::JoinType::Miter,
        clipper2next::benchmarks::legacy::EndType::Polygon);
    const auto actual = execute_next_offset(
        fixture.next_subject, 25.0, next::JoinType::Miter, next::EndType::Polygon);

    EXPECT_NO_THROW(clipper2next::benchmarks::assert_same_paths(expected, actual));
}

TEST(Clipper2NextDifferentialOffsetTests, RepeatedBenchmarkFixtureOffsetStaysStable) {
    const auto fixture = clipper2next::benchmarks::make_offset_fixture();
    const auto expected = clipper2next::benchmarks::legacy::InflatePaths(
        fixture.legacy_subject,
        25.0,
        clipper2next::benchmarks::legacy::JoinType::Miter,
        clipper2next::benchmarks::legacy::EndType::Polygon);

    for (int iteration = 0; iteration < 1000; ++iteration) {
        const auto actual = execute_next_offset(
            fixture.next_subject, 25.0, next::JoinType::Miter, next::EndType::Polygon);
        EXPECT_NO_THROW(clipper2next::benchmarks::assert_same_paths(expected, actual)) << iteration;
    }
}

TEST(Clipper2NextDifferentialOffsetTests, PositiveSquarePolyTreeOffsetAreaMatchesLegacy) {
    const auto legacy_subject = legacy::Paths64{
        legacy::MakePath({0, 0, 100, 0, 100, 100, 0, 100}),
    };
    const auto next_subject = next::Paths64{
        {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };

    legacy::ClipperOffset legacy_offset;
    legacy_offset.AddPaths(legacy_subject, legacy::JoinType::Miter, legacy::EndType::Polygon);
    legacy::PolyTree64 legacy_tree;
    legacy_offset.Execute(10.0, legacy_tree);
    const auto legacy_paths = legacy::PolyTreeToPaths64(legacy_tree);

    next::offset_builder next_offset;
    next_offset.delta(10.0)
        .join(next::JoinType::Miter)
        .end(next::EndType::Polygon)
        .add(next_subject);
    next::PolyTree64 next_tree;
    next_offset.execute_into(next_tree);
    const auto next_paths = collect_tree_paths(next_tree);

    ASSERT_EQ(legacy_paths.size(), next_paths.size());
    EXPECT_EQ(legacy::Area(legacy_paths), next::area(next_paths));
}
