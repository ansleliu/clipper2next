#include <gtest/gtest.h>

#include "path_equivalence.h"
#include "support/test_paths.h"

#include "clipper2/clipper.h"
#include "clipper2next/minkowski.h"

#include <string>
#include <vector>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;
namespace test = clipper2next::tests;

namespace {

[[nodiscard]] auto execute_next_sum(const next::Path64& pattern,
                                    const next::Path64& path,
                                    bool is_closed) -> next::Paths64 {
    next::minkowski_request64 request;
    request.pattern = pattern;
    request.path = path;
    request.is_closed = is_closed;
    return next::minkowski_sum(request);
}

[[nodiscard]] auto execute_next_difference(const next::Path64& pattern,
                                           const next::Path64& path,
                                           bool is_closed) -> next::Paths64 {
    next::minkowski_request64 request;
    request.pattern = pattern;
    request.path = path;
    request.is_closed = is_closed;
    return next::minkowski_difference(request);
}

auto assert_minkowski_sum_matches_legacy(const next::Path64& pattern,
                                         const next::Path64& path,
                                         bool is_closed) -> void {
    const auto expected = legacy::MinkowskiSum(
        oracle::to_legacy_path(pattern), oracle::to_legacy_path(path), is_closed);
    const auto actual = execute_next_sum(pattern, path, is_closed);
    oracle::assert_paths_semantically_equal(expected, actual);
}

auto assert_minkowski_difference_matches_legacy(const next::Path64& pattern,
                                                const next::Path64& path,
                                                bool is_closed) -> void {
    const auto expected = legacy::MinkowskiDiff(
        oracle::to_legacy_path(pattern), oracle::to_legacy_path(path), is_closed);
    const auto actual = execute_next_difference(pattern, path, is_closed);
    oracle::assert_paths_semantically_equal(expected, actual);
}

}  // namespace

TEST(Clipper2NextDifferentialMinkowskiTests, ClosedSumMatchesLegacy) {
    const auto pattern = test::path64({0, 0, 80, 0, 80, 40, 0, 40});
    const auto path = test::path64({100, 100, 220, 140, 180, 260, 60, 220});

    assert_minkowski_sum_matches_legacy(pattern, path, true);
}

TEST(Clipper2NextDifferentialMinkowskiTests, ClosedDifferenceMatchesLegacy) {
    const auto pattern = test::path64({0, 0, 60, 0, 80, 40, 20, 80, -20, 40});
    const auto path = test::path64({100, 100, 260, 100, 300, 220, 160, 300, 80, 200});

    assert_minkowski_difference_matches_legacy(pattern, path, true);
}

TEST(Clipper2NextDifferentialMinkowskiTests, OpenSumMatchesLegacy) {
    const auto pattern = test::path64({-20, -10, 20, -10, 20, 10, -20, 10});
    const auto path = test::path64({0, 0, 80, 20, 120, 90, 220, 120});

    assert_minkowski_sum_matches_legacy(pattern, path, false);
}

TEST(Clipper2NextDifferentialMinkowskiTests, OpenDifferenceMatchesLegacy) {
    const auto pattern = test::path64({-30, 0, 0, -25, 30, 0, 0, 25});
    const auto path = test::path64({0, 0, 70, 10, 120, 80, 190, 30, 260, 100});

    assert_minkowski_difference_matches_legacy(pattern, path, false);
}

TEST(Clipper2NextDifferentialMinkowskiTests, GeneratedSumCorpusMatchesLegacy) {
    const std::vector<next::Path64> patterns{test::path64({-10, -10, 25, -10, 25, 15, -10, 15}),
                                             test::path64({0, -20, 20, 0, 0, 20, -20, 0}),
                                             test::path64({-30, 0, 0, -15, 30, 0, 12, 25, -12, 25})};
    const std::vector<next::Path64> paths{
        test::path64({0, 0, 90, 10, 130, 80, 40, 120}),
        test::path64({200, 30, 260, 30, 300, 90, 240, 150, 180, 100}),
        test::path64({0, 220, 50, 260, 100, 230, 160, 280, 210, 250})};

    for (std::size_t pattern_index = 0; pattern_index < patterns.size(); ++pattern_index) {
        for (std::size_t path_index = 0; path_index < paths.size(); ++path_index) {
            for (const auto is_closed : {true, false}) {
                SCOPED_TRACE("pattern=" + std::to_string(pattern_index) + " path=" +
                             std::to_string(path_index) + " closed=" + std::to_string(is_closed));
                EXPECT_NO_THROW(assert_minkowski_sum_matches_legacy(
                    patterns[pattern_index], paths[path_index], is_closed));
            }
        }
    }
}

TEST(Clipper2NextDifferentialMinkowskiTests, GeneratedDifferenceCorpusMatchesLegacy) {
    const std::vector<next::Path64> patterns{test::path64({-12, -8, 18, -8, 24, 18, -18, 18}),
                                             test::path64({0, -24, 28, 0, 0, 24, -28, 0})};
    const std::vector<next::Path64> paths{
        test::path64({300, 0, 390, 20, 430, 110, 330, 140, 280, 80}),
        test::path64({100, 260, 150, 310, 240, 270, 300, 330, 360, 290})};

    for (std::size_t pattern_index = 0; pattern_index < patterns.size(); ++pattern_index) {
        for (std::size_t path_index = 0; path_index < paths.size(); ++path_index) {
            for (const auto is_closed : {true, false}) {
                SCOPED_TRACE("pattern=" + std::to_string(pattern_index) + " path=" +
                             std::to_string(path_index) + " closed=" + std::to_string(is_closed));
                EXPECT_NO_THROW(assert_minkowski_difference_matches_legacy(
                    patterns[pattern_index], paths[path_index], is_closed));
            }
        }
    }
}

TEST(Clipper2NextDifferentialMinkowskiTests, ExternalStyleTranslatedStressCorpusMatchesLegacy) {
    const std::vector<next::Path64> patterns{test::path64({-8, -8, 18, -8, 24, 0, 18, 16, -8, 16}),
                                             test::path64({0, -18, 18, -6, 12, 18, -12, 18, -18, -6})};
    const std::vector<next::Path64> paths{
        test::path64({0, 0, 120, 0, 140, 60, 90, 110, 30, 100, -20, 40}),
        test::path64({0, 0, 70, -20, 130, 20, 170, 90, 110, 150, 30, 120, -30, 60})};

    for (std::size_t pattern_index = 0; pattern_index < patterns.size(); ++pattern_index) {
        for (std::size_t path_index = 0; path_index < paths.size(); ++path_index) {
            for (int translate_index = 0; translate_index < 6; ++translate_index) {
                const auto dx = static_cast<int64_t>(translate_index * 10000 - 25000);
                const auto dy = static_cast<int64_t>((translate_index % 3) * -7000 + 9000);
                auto translated_pattern = patterns[pattern_index];
                auto translated_path = paths[path_index];
                for (auto& point : translated_pattern) {
                    point.x += dx / 10;
                    point.y += dy / 10;
                }
                for (auto& point : translated_path) {
                    point.x += dx;
                    point.y += dy;
                }

                SCOPED_TRACE("pattern=" + std::to_string(pattern_index) +
                             " path=" + std::to_string(path_index) +
                             " translate=" + std::to_string(translate_index));
                EXPECT_NO_THROW(
                    assert_minkowski_sum_matches_legacy(translated_pattern, translated_path, true));
                EXPECT_NO_THROW(assert_minkowski_difference_matches_legacy(
                    translated_pattern, translated_path, false));
            }
        }
    }
}

TEST(Clipper2NextDifferentialMinkowskiTests, LongerGeneratedOpenClosedCorpusMatchesLegacy) {
    const std::vector<next::Path64> patterns{
        test::path64({-24, -12, 12, -24, 42, -6, 36, 28, 0, 36, -30, 18}),
        test::path64({-18, -24, 18, -24, 34, 0, 20, 30, -16, 30, -36, 0}),
        test::path64({-40, -8, -12, -30, 24, -22, 44, 8, 18, 34, -26, 24})};
    const std::vector<next::Path64> paths{
        test::path64({0, 0, 80, -20, 150, 10, 190, 80, 150, 150, 70, 170, -20, 110, -40, 40}),
        test::path64({300, 20, 370, -10, 460, 40, 480, 130, 400, 210, 310, 180, 260, 90}),
        test::path64({-220, 60, -130, 10, -40, 50, -20, 150, -100, 230, -210, 190, -260, 120}),
        test::path64({80, 320, 150, 280, 250, 300, 300, 380, 260, 470, 150, 500, 50, 440, 30, 360})};

    for (std::size_t pattern_index = 0; pattern_index < patterns.size(); ++pattern_index) {
        for (std::size_t path_index = 0; path_index < paths.size(); ++path_index) {
            for (const auto is_closed : {true, false}) {
                SCOPED_TRACE("pattern=" + std::to_string(pattern_index) + " path=" +
                             std::to_string(path_index) + " closed=" + std::to_string(is_closed));
                EXPECT_NO_THROW(assert_minkowski_sum_matches_legacy(
                    patterns[pattern_index], paths[path_index], is_closed));
                EXPECT_NO_THROW(assert_minkowski_difference_matches_legacy(
                    patterns[pattern_index], paths[path_index], is_closed));
            }
        }
    }
}
