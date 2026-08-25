#include <gtest/gtest.h>

#include "path_equivalence.h"
#include "support/test_paths.h"

#include "clipper2/clipper.triangulation.h"
#include "clipper2next/triangulation.h"

#include <string>
#include <vector>

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;
namespace test = clipper2next::tests;

namespace {

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

[[nodiscard]] auto execute_next_triangulation(const next::Paths64& paths, bool use_delaunay)
    -> next::triangulation_result64 {
    next::triangulation_request64 request;
    request.paths = paths;
    request.use_delaunay = use_delaunay;
    return next::triangulate(request);
}

auto assert_triangulation_matches_legacy(const next::Paths64& paths, bool use_delaunay) -> void {
    legacy::Paths64 expected_triangles;
    const auto legacy_status =
        legacy::Triangulate(oracle::to_legacy_paths(paths), expected_triangles, use_delaunay);
    const auto actual = execute_next_triangulation(paths, use_delaunay);

    ASSERT_EQ(to_next_status(legacy_status), actual.status);
    if (actual.status == next::TriangulateResult::success) {
        oracle::assert_paths_semantically_equal(expected_triangles, actual.triangles);
    } else {
        EXPECT_TRUE(actual.triangles.empty());
    }
}

}  // namespace

TEST(Clipper2NextDifferentialTriangulationTests, GeneratedSimpleCorpusMatchesLegacy) {
    const std::vector<next::Paths64> cases{
        {test::path64({0, 0, 120, 0, 120, 80, 0, 80})},
        {test::path64({0, 0, 160, 0, 160, 50, 90, 50, 90, 110, 0, 110})},
        {test::path64({-80, -30, 80, -40, 140, 40, 50, 120, -90, 80})},
        {
            test::path64({0, 0, 300, 0, 300, 260, 0, 260}),
            test::path64({80, 80, 80, 180, 220, 180, 220, 80}),
        },
    };

    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        for (const auto use_delaunay : {false, true}) {
            SCOPED_TRACE("case=" + std::to_string(case_index) +
                         " delaunay=" + std::to_string(use_delaunay));
            assert_triangulation_matches_legacy(cases[case_index], use_delaunay);
        }
    }
}

TEST(Clipper2NextDifferentialTriangulationTests, GeneratedStatusCorpusMatchesLegacy) {
    const std::vector<next::Paths64> cases{
        {},
        {test::path64({0, 0, 100, 100})},
        {test::path64({0, 0, 120, 120, 0, 120, 120, 0})},
    };

    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        SCOPED_TRACE(case_index);
        assert_triangulation_matches_legacy(cases[case_index], true);
    }
}
