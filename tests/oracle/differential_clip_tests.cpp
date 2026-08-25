#include <gtest/gtest.h>

#include "../support/random_path_generator.h"
#include "path_equivalence.h"
#include "support/test_paths.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;
namespace support = clipper2next::tests::support;
namespace test = clipper2next::tests;

namespace {

[[nodiscard]] auto to_legacy_clip_type(next::ClipType clip_type) -> legacy::ClipType {
    switch (clip_type) {
    case next::ClipType::NoClip: {
        return legacy::ClipType::NoClip;
    }
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
    }
    return legacy::ClipType::NoClip;
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

[[nodiscard]] auto execute_legacy_clip(next::ClipType clip_type,
                                       next::FillRule fill_rule,
                                       const next::Paths64& subjects,
                                       const next::Paths64& clips) -> legacy::Paths64 {
    legacy::Clipper64 clipper;
    clipper.AddSubject(oracle::to_legacy_paths(subjects));
    clipper.AddClip(oracle::to_legacy_paths(clips));
    legacy::Paths64 solution;
    clipper.Execute(to_legacy_clip_type(clip_type), to_legacy_fill_rule(fill_rule), solution);
    return solution;
}

[[nodiscard]] auto execute_legacy_clip(next::ClipType clip_type,
                                       const next::Paths64& subjects,
                                       const next::Paths64& clips) -> legacy::Paths64 {
    return execute_legacy_clip(clip_type, next::FillRule::NonZero, subjects, clips);
}

[[nodiscard]] auto execute_next_clip(next::ClipType clip_type,
                                     next::FillRule fill_rule,
                                     const next::Paths64& subjects,
                                     const next::Paths64& clips = {}) -> next::Paths64 {
    next::clip_request64 request;
    request.clip_type = clip_type;
    request.fill_rule = fill_rule;
    request.subjects = subjects;
    request.clips = clips;
    return next::clip(request).closed;
}

[[nodiscard]] auto execute_next_clip(next::ClipType clip_type,
                                     const next::Paths64& subjects,
                                     const next::Paths64& clips = {}) -> next::Paths64 {
    return execute_next_clip(clip_type, next::FillRule::NonZero, subjects, clips);
}

[[nodiscard]] auto generated_random_rectangle(support::random_path_generator& generator)
    -> next::Path64 {
    const auto left = static_cast<int64_t>(generator.integer(-500, 500));
    const auto top = static_cast<int64_t>(generator.integer(-500, 500));
    const auto width = static_cast<int64_t>(generator.integer(1, 90));
    const auto height = static_cast<int64_t>(generator.integer(1, 90));
    next::Path64 path{{left, top}, {left + width, top}, {left + width, top + height}, {left, top + height}};
    if (generator.integer(0, 1) == 0) { std::reverse(path.begin(), path.end()); }
    return path;
}

[[nodiscard]] auto generated_random_rectangles(support::random_path_generator& generator,
                                               int minimum_count) -> next::Paths64 {
    const auto count = generator.integer(minimum_count, minimum_count + 3);
    next::Paths64 paths;
    paths.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        paths.push_back(generated_random_rectangle(generator));
    }
    return paths;
}

[[nodiscard]] auto generated_random_polygon(support::random_path_generator& generator)
    -> next::Path64 {
    const auto center_x = static_cast<int64_t>(generator.integer(-600, 600));
    const auto center_y = static_cast<int64_t>(generator.integer(-600, 600));
    const auto width = static_cast<int64_t>(generator.integer(40, 180));
    const auto height = static_cast<int64_t>(generator.integer(40, 180));

    next::Path64 path{
        {center_x - width, center_y - generator.integer(0, static_cast<int>(height / 3))},
        {center_x - generator.integer(static_cast<int>(width / 3), static_cast<int>(width)),
         center_y - height},
        {center_x + generator.integer(0, static_cast<int>(width / 2)),
         center_y - generator.integer(static_cast<int>(height / 2), static_cast<int>(height))},
        {center_x + width, center_y - generator.integer(0, static_cast<int>(height / 3))},
        {center_x + generator.integer(static_cast<int>(width / 3), static_cast<int>(width)),
         center_y + height},
        {center_x - generator.integer(0, static_cast<int>(width / 2)),
         center_y + generator.integer(static_cast<int>(height / 2), static_cast<int>(height))},
        {center_x - width, center_y + generator.integer(0, static_cast<int>(height / 3))},
    };
    if (generator.integer(0, 1) == 0) { std::reverse(path.begin(), path.end()); }
    return path;
}

[[nodiscard]] auto generated_random_polygons(support::random_path_generator& generator,
                                             int minimum_count) -> next::Paths64 {
    const auto count = generator.integer(minimum_count, minimum_count + 3);
    next::Paths64 paths;
    paths.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        paths.push_back(generated_random_polygon(generator));
    }
    return paths;
}

[[nodiscard]] auto generated_random_open_path(support::random_path_generator& generator)
    -> next::Path64 {
    const auto point_count = generator.integer(2, 8);
    next::Path64 path;
    path.reserve(static_cast<std::size_t>(point_count));
    int64_t x = static_cast<int64_t>(generator.integer(-700, 700));
    int64_t y = static_cast<int64_t>(generator.integer(-700, 700));
    path.emplace_back(x, y);
    for (int index = 1; index < point_count; ++index) {
        x += static_cast<int64_t>(generator.integer(-180, 180));
        y += static_cast<int64_t>(generator.integer(-180, 180));
        path.emplace_back(x, y);
    }
    return path;
}

[[nodiscard]] auto generated_random_open_paths(support::random_path_generator& generator)
    -> next::Paths64 {
    const auto count = generator.integer(1, 4);
    next::Paths64 paths;
    paths.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        paths.push_back(generated_random_open_path(generator));
    }
    return paths;
}

[[nodiscard]] auto execute_legacy_open_clip(const next::Paths64& open_subjects,
                                            const next::Paths64& clips) -> legacy::Paths64 {
    legacy::Clipper64 clipper;
    clipper.AddOpenSubject(oracle::to_legacy_paths(open_subjects));
    clipper.AddClip(oracle::to_legacy_paths(clips));
    legacy::Paths64 closed_solution;
    legacy::Paths64 open_solution;
    clipper.Execute(
        legacy::ClipType::Intersection, legacy::FillRule::NonZero, closed_solution, open_solution);
    return open_solution;
}

[[nodiscard]] auto execute_next_open_clip(const next::Paths64& open_subjects,
                                          const next::Paths64& clips) -> next::Paths64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.open_subjects = open_subjects;
    request.clips = clips;
    return next::clip(request).open;
}

auto collect_tree_paths(const next::PolyTree64& tree,
                        next::PolyTree64::node_id node,
                        next::Paths64& paths) -> void {
    for (const auto child : tree.children(node)) {
        paths.push_back(tree.polygon(child));
        collect_tree_paths(tree, child, paths);
    }
}

[[nodiscard]] auto collect_tree_paths(const next::PolyTree64& tree) -> next::Paths64 {
    next::Paths64 paths;
    collect_tree_paths(tree, tree.root(), paths);
    return paths;
}

struct oracle_topology_sink final {
    auto begin(const next::topology_layout64& layout) -> next::clipper_error_code {
        polygons.assign(layout.polygons.begin(), layout.polygons.end());
        paths.reserve(layout.ring_count);
        rings.reserve(layout.ring_count);
        return next::clipper_error_code::ok;
    }

    auto acquire(const next::topology_ring_layout64& ring,
                 std::span<geotypes::Point2i64>& destination)
        -> next::clipper_error_code {
        rings.push_back(ring);
        paths.emplace_back(ring.point_count);
        destination = paths.back();
        return next::clipper_error_code::ok;
    }

    auto finish() -> next::clipper_error_code {
        return next::clipper_error_code::ok;
    }

    auto cancel() noexcept -> void {
        polygons.clear();
        rings.clear();
        paths.clear();
    }

    std::vector<next::topology_polygon_layout64> polygons{};
    std::vector<next::topology_ring_layout64> rings{};
    next::Paths64 paths{};
};

}  // namespace

TEST(Clipper2NextDifferentialClipTests, UnionOfOverlappingRectanglesMatchesLegacy) {
    const auto legacy_subject = legacy::Paths64{
        legacy::MakePath({0, 0, 100, 0, 100, 100, 0, 100}),
        legacy::MakePath({50, 50, 150, 50, 150, 150, 50, 150}),
    };
    const auto next_subject = next::Paths64{
        {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
        {{50, 50}, {150, 50}, {150, 150}, {50, 150}},
    };

    const auto expected = legacy::Union(legacy_subject, legacy::FillRule::NonZero);
    const auto actual = execute_next_clip(next::ClipType::Union, next_subject);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialClipTests, UnionOfPartialEdgeTouchingRectanglesMatchesLegacy) {
    const auto subjects = next::Paths64{
        test::path64({0, 0, 75000, 0, 75000, 75000, 0, 75000}),
    };
    const auto clips = next::Paths64{
        test::path64({75000, 25000, 100000, 25000, 100000, 50000, 75000, 50000}),
    };

    const auto expected = execute_legacy_clip(next::ClipType::Union, subjects, clips);
    const auto actual = execute_next_clip(next::ClipType::Union, subjects, clips);

    ASSERT_EQ(expected.size(), 1U);
    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialClipTests, DifferenceWithHoleMatchesLegacy) {
    const auto legacy_subject = legacy::Paths64{
        legacy::MakePath({0, 0, 200, 0, 200, 200, 0, 200}),
    };
    const auto legacy_clip = legacy::Paths64{
        legacy::MakePath({50, 50, 150, 50, 150, 150, 50, 150}),
    };
    const auto next_subject = next::Paths64{
        {{0, 0}, {200, 0}, {200, 200}, {0, 200}},
    };
    const auto next_clip = next::Paths64{
        {{50, 50}, {150, 50}, {150, 150}, {50, 150}},
    };

    const auto expected =
        legacy::Difference(legacy_subject, legacy_clip, legacy::FillRule::NonZero);
    const auto actual = execute_next_clip(next::ClipType::Difference, next_subject, next_clip);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialClipTests, XorOfCornerOverlappingRectanglesMatchesLegacy) {
    const auto subjects = next::Paths64{
        test::path64({0, 0, 1000, 0, 1000, 1000, 0, 1000}),
    };
    const auto clips = next::Paths64{
        test::path64({250, 250, 1250, 250, 1250, 1250, 250, 1250}),
    };

    const auto expected = execute_legacy_clip(next::ClipType::Xor, subjects, clips);
    const auto actual = execute_next_clip(next::ClipType::Xor, subjects, clips);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialClipTests,
     GeoTypesTopologyViewAndDirectWriterMatchLegacyWithoutTolerance) {
    const auto subject_points = std::array{
        geotypes::Point2i64{0, 0},
        geotypes::Point2i64{200, 0},
        geotypes::Point2i64{200, 200},
        geotypes::Point2i64{0, 200},
    };
    const auto subject_rings = std::array{
        geotypes::RingDescriptor{0U, 4U, geotypes::RingRole::Shell},
    };
    const auto subject_polygons = std::array{
        geotypes::PolygonDescriptor{0U, 1U, geotypes::noPolygonIndex},
    };
    const auto clips = next::Paths64{
        {{50, 50}, {150, 50}, {150, 150}, {50, 150}},
    };
    const auto expected = execute_legacy_clip(
        next::ClipType::Difference,
        next::Paths64{next::Path64{subject_points.begin(), subject_points.end()}},
        clips);

    auto sink = oracle_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Difference;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::borrow_paths64(
        geotypes::TopologyView64{subject_points, subject_rings, subject_polygons});
    request.clips = next::borrow_paths64(clips);
    const auto actual = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_TRUE(actual.has_value()) << static_cast<int>(actual.error());
    EXPECT_EQ(actual->input_collection_point_writes, 0U);
    ASSERT_EQ(sink.polygons.size(), 1U);
    EXPECT_EQ(sink.polygons.front().parent_polygon_index,
              next::topology_no_polygon_index);
    EXPECT_EQ(sink.polygons.front().ring_count, 2U);
    ASSERT_EQ(sink.rings.size(), 2U);
    EXPECT_EQ(sink.rings[0].role, next::topology_ring_role::shell);
    EXPECT_EQ(sink.rings[1].role, next::topology_ring_role::hole);
    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, sink.paths));
}

TEST(Clipper2NextDifferentialClipTests, XorOfPartialEdgeTouchingRectanglesMatchesLegacy) {
    const auto subjects = next::Paths64{
        test::path64({0, 0, 75000, 0, 75000, 75000, 0, 75000}),
    };
    const auto clips = next::Paths64{
        test::path64({75000, 25000, 100000, 25000, 100000, 50000, 75000, 50000}),
    };

    const auto expected = execute_legacy_clip(next::ClipType::Xor, subjects, clips);
    const auto actual = execute_next_clip(next::ClipType::Xor, subjects, clips);

    ASSERT_EQ(expected.size(), 1U);
    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialClipTests, NestedRectangleUnionTreeMatchesLegacy) {
    const auto subjects = next::Paths64{
        test::path64({0, 0, 1000, 0, 1000, 1000, 0, 1000}),
        test::path64({100, 100, 900, 100, 900, 900, 100, 900}),
        test::path64({200, 200, 800, 200, 800, 800, 200, 800}),
    };
    legacy::Clipper64 clipper;
    clipper.AddSubject(oracle::to_legacy_paths(subjects));
    legacy::PolyTree64 legacy_tree;
    clipper.Execute(legacy::ClipType::Union, legacy::FillRule::NonZero, legacy_tree);

    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = subjects;
    const auto actual = collect_tree_paths(next::clip_tree(request).tree);

    EXPECT_NO_THROW(
        oracle::assert_paths_semantically_equal(legacy::PolyTreeToPaths64(legacy_tree), actual));
}

TEST(Clipper2NextDifferentialClipTests, ClipTreeShapeNestedUnionMatchesLegacy) {
    const auto subjects = next::Paths64{
        test::path64({0, 0, 1000, 0, 1000, 1000, 0, 1000}),
        test::path64({100, 100, 900, 100, 900, 900, 100, 900}),
        test::path64({200, 200, 800, 200, 800, 800, 200, 800}),
    };
    legacy::Clipper64 clipper;
    clipper.AddSubject(oracle::to_legacy_paths(subjects));
    legacy::PolyTree64 legacy_tree;
    clipper.Execute(legacy::ClipType::Union, legacy::FillRule::NonZero, legacy_tree);

    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = subjects;
    const auto actual = collect_tree_paths(next::clip_tree(request).tree);

    EXPECT_NO_THROW(
        oracle::assert_paths_semantically_equal(legacy::PolyTreeToPaths64(legacy_tree), actual));
}

TEST(Clipper2NextDifferentialClipTests, OpenLineRectangleIntersectionMatchesLegacy) {
    const auto lines = next::Paths64{
        test::path64({-20, 50, 50, 50, 120, 50}),
        test::path64({20, -20, 20, 20, 20, 120}),
    };
    const auto clips = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto expected = execute_legacy_open_clip(lines, clips);
    const auto actual = execute_next_open_clip(lines, clips);

    EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(expected, actual));
}

TEST(Clipper2NextDifferentialClipTests, AllBooleanOperationsGeneratedCorpusMatchesLegacy) {
    const std::vector<next::ClipType> operations{next::ClipType::Intersection,
                                                 next::ClipType::Union,
                                                 next::ClipType::Difference,
                                                 next::ClipType::Xor};
    const std::vector<next::Paths64> subjects{
        {test::path64({0, 0, 160, 0, 160, 120, 0, 120})},
        {test::path64({0, 0, 180, 0, 180, 50, 110, 50, 110, 140, 0, 140})},
        {
            test::path64({-80, -20, 70, -20, 70, 90, -80, 90}),
            test::path64({100, 20, 220, 20, 220, 130, 100, 130}),
        }};
    const std::vector<next::Paths64> clips{{test::path64({40, -30, 210, -30, 210, 90, 40, 90})},
                                           {test::path64({40, 30, 150, 30, 150, 180, 40, 180})},
                                           {
                                               test::path64({-30, -40, 130, -40, 130, 150, -30, 150}),
                                               test::path64({150, 0, 250, 0, 250, 180, 150, 180}),
                                           }};

    for (const auto operation : operations) {
        for (std::size_t subject_index = 0; subject_index < subjects.size(); ++subject_index) {
            for (std::size_t clip_index = 0; clip_index < clips.size(); ++clip_index) {
                SCOPED_TRACE("operation=" + std::to_string(static_cast<int>(operation)) +
                             " subject=" + std::to_string(subject_index) +
                             " clip=" + std::to_string(clip_index));
                const auto expected =
                    execute_legacy_clip(operation, subjects[subject_index], clips[clip_index]);
                const auto actual =
                    execute_next_clip(operation, subjects[subject_index], clips[clip_index]);
                EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
            }
        }
    }
}

TEST(Clipper2NextDifferentialClipTests, RandomizedClosedRectanglesAllFillRulesMatchLegacy) {
    for (std::uint32_t seed = 1; seed <= 96; ++seed) {
        support::random_path_generator generator{seed};
        const auto operation = generator.clip_type();
        const auto fill_rule = generator.fill_rule();
        const auto subjects = generated_random_rectangles(generator, 1);
        const auto clips = generated_random_rectangles(generator, operation == next::ClipType::Union ? 0 : 1);

        SCOPED_TRACE("seed=" + std::to_string(seed) +
                     " operation=" + std::to_string(static_cast<int>(operation)) +
                     " fill_rule=" + std::to_string(static_cast<int>(fill_rule)));
        const auto expected = execute_legacy_clip(operation, fill_rule, subjects, clips);
        const auto actual = execute_next_clip(operation, fill_rule, subjects, clips);
        EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
    }
}

TEST(Clipper2NextDifferentialClipTests,
     AdjacentMicroSelfIntersectionXorMatchesLegacy) {
    const auto subjects = next::Paths64{
        {{-401, 402},
         {-339, 310},
         {-280, 334},
         {-171, 435},
         {-180, 560},
         {-338, 547},
         {-401, 468}},
        {{-573, 162},
         {-465, 264},
         {-351, 268},
         {-307, 154},
         {-428, 80},
         {-556, 42},
         {-573, 134}},
        {{-418, 409},
         {-349, 476},
         {-232, 503},
         {-228, 380},
         {-314, 320},
         {-412, 301},
         {-418, 384}},
    };
    const auto expected = execute_legacy_clip(
        next::ClipType::Xor, next::FillRule::NonZero, subjects, {});
    const auto actual = execute_next_clip(
        next::ClipType::Xor, next::FillRule::NonZero, subjects, {});

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextDifferentialClipTests, SmallTriangleUnionMatchesLegacyExactly) {
    const auto subjects = next::Paths64{
        {{4, 4}, {0, 1}, {1, 1}},
    };
    const auto expected = execute_legacy_clip(next::ClipType::Union, subjects, {});
    const auto actual = execute_next_clip(next::ClipType::Union, subjects);

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
    EXPECT_TRUE(expected.empty());
}

TEST(Clipper2NextDifferentialClipTests, RandomizedClosedPolygonsAllFillRulesMatchLegacy) {
    const std::vector<next::ClipType> operations{next::ClipType::Intersection,
                                                 next::ClipType::Union,
                                                 next::ClipType::Difference,
                                                 next::ClipType::Xor};
    const std::vector<next::FillRule> fill_rules{next::FillRule::EvenOdd,
                                                 next::FillRule::NonZero,
                                                 next::FillRule::Positive,
                                                 next::FillRule::Negative};

    for (std::uint32_t seed = 1; seed <= 128; ++seed) {
        for (const auto operation : operations) {
            for (const auto fill_rule : fill_rules) {
                support::random_path_generator generator{
                    seed * 257U + static_cast<std::uint32_t>(operation) * 17U +
                    static_cast<std::uint32_t>(fill_rule)};
                const auto subjects = generated_random_polygons(generator, 1);
                const auto clips =
                    generated_random_polygons(generator, operation == next::ClipType::Union ? 0 : 1);

                SCOPED_TRACE("seed=" + std::to_string(seed) +
                             " operation=" + std::to_string(static_cast<int>(operation)) +
                             " fill_rule=" + std::to_string(static_cast<int>(fill_rule)));
                const auto expected = execute_legacy_clip(operation, fill_rule, subjects, clips);
                const auto actual = execute_next_clip(operation, fill_rule, subjects, clips);
                EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
            }
        }
    }
}

TEST(Clipper2NextDifferentialClipTests, RandomizedOpenSubjectsMatchLegacy) {
    for (std::uint32_t seed = 1; seed <= 192; ++seed) {
        support::random_path_generator generator{seed * 389U};
        const auto open_subjects = generated_random_open_paths(generator);
        const auto clips = generated_random_polygons(generator, 1);

        SCOPED_TRACE("seed=" + std::to_string(seed));
        const auto expected = execute_legacy_open_clip(open_subjects, clips);
        const auto actual = execute_next_open_clip(open_subjects, clips);
        EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(expected, actual));
    }
}
