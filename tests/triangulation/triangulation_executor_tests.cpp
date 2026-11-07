#include <gtest/gtest.h>
#include <algorithm>

#include "triangulation/private/triangulation_executor.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_result_builder.h"
#include "clipper2next/triangulation.h"
#include "triangulation/private/triangulation_cache.h"
#include "triangulation/private/triangulation_operation.h"
#include "clipper2next/api/memory.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationExecutorTests, TriangulatesSimpleSquare) {
    next::TriangulateResult result = next::TriangulateResult::fail;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
    };

    const auto triangles = next::internal::execute_triangulation(paths, true, result);

    EXPECT_EQ(result, next::TriangulateResult::success);
    EXPECT_FALSE(triangles.empty());
}

namespace {
auto small_area2(const next::Paths64& triangles) -> int64_t {
    int64_t sum = 0;
    for (const auto& t : triangles) {
        if (t.size() != 3) {
            ADD_FAILURE();
            continue;
        }
        const auto a =
            (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x);
        EXPECT_NE(a, 0);
        sum += a < 0 ? -a : a;
    }
    return sum;
}

auto cache_sized_request(int64_t offset = 0) -> next::triangulation_request64 {
    next::triangulation_request64 request;
    for (int64_t i = 0; i != 128; ++i) {
        const auto x = offset + i * 200;
        request.paths.push_back({{x, 0}, {x + 100, 0}, {x + 100, 100}, {x, 100}});
    }
    return request;
}
}  // namespace

TEST(Clipper2NextTriangulationExecutorTests, PreservesUnitEdgesAndTouchCoverage) {
    const std::vector<std::pair<next::Paths64, int64_t>> cases{
        {{{{0, 0}, {1, 0}, {0, 5}}}, 5},
        {{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, {{50, 50}, {50, 51}, {55, 50}}}, 19995},
        {{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{0, 5}, {2, 7}, {2, 3}}}, 192},
        {{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{10, 10}, {20, 10}, {20, 20}, {10, 20}}}, 400},
        {{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{10, 3}, {20, 3}, {20, 7}, {10, 7}}}, 280},
    };
    for (const auto& [paths, expected] : cases) {
        for (bool delaunay : {false, true}) {
            SCOPED_TRACE(expected);
            auto result = next::TriangulateResult::fail;
            const auto triangles = next::internal::execute_triangulation(paths, delaunay, result);
            EXPECT_EQ(result, next::TriangulateResult::success);
            EXPECT_EQ(small_area2(triangles), expected);
        }
    }
}

TEST(Clipper2NextTriangulationExecutorTests, RejectsProperCrossingsAtEveryScale) {
    for (int64_t scale : {1, 100}) {
        const next::Paths64 paths{{{0, 0}, {scale, scale}, {0, scale}, {scale, 0}}};
        for (bool delaunay : {false, true}) {
            auto result = next::TriangulateResult::fail;
            const auto triangles = next::internal::execute_triangulation(paths, delaunay, result);
            EXPECT_EQ(result, next::TriangulateResult::paths_intersect);
            EXPECT_TRUE(triangles.empty());
        }
    }
}

TEST(Clipper2NextTriangulationExecutorTests, InvalidRolesAreTypedAndReuseDoesNotRetainFailure) {
    next::triangulation_request64 request;
    request.paths = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{2, 2}, {8, 2}, {8, 8}, {2, 8}}};
    const auto invalid = next::triangulate_checked(request);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), next::clipper_error_code::invalid_argument);
    request.paths = {{{0, 0}, {1, 0}, {0, 5}}};
    const auto valid = next::triangulate_checked(request);
    ASSERT_TRUE(valid.has_value());
    EXPECT_EQ(valid->status, next::TriangulateResult::success);
    EXPECT_EQ(small_area2(valid->triangles), 5);
}

TEST(Clipper2NextTriangulationExecutorTests, InvalidRolesRemainTypedThroughDoubleScaling) {
    next::triangulation_requestd request;
    request.decimal_precision = 1;
    request.paths = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}},
                     {{0.25, 0.25}, {0.75, 0.25}, {0.75, 0.75}, {0.25, 0.75}}};
    const auto result = next::triangulate_checked(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::invalid_argument);
}

TEST(Clipper2NextTriangulationExecutorTests, CacheCannotConvertFailureOrEmptyToSuccess) {
    next::internal::release_triangulation_thread_state();
    auto operation = next::internal::triangulation_operation_lease{};
    static_cast<void>(operation.context());
    next::triangulation_request64 request;
    request.paths.resize(1);
    for (int i = 0; i < 513; ++i) { request.paths.front().push_back({i, i}); }
    next::triangulation_result64 result;
    for (auto status : {next::TriangulateResult::fail,
                        next::TriangulateResult::no_polygons,
                        next::TriangulateResult::paths_intersect,
                        next::TriangulateResult::success}) {
        result.status = status;
        next::internal::store_cached_triangulation_graph(operation, request, status, {});
        EXPECT_FALSE(next::internal::try_get_cached_triangulation(operation, request, result));
    }
}

TEST(Clipper2NextTriangulationExecutorTests, NestedExecutionPreservesOuterResultSource) {
    const next::Paths64 outer_paths{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    const next::Paths64 inner_paths{{{1000, 1000}, {1040, 1000}, {1000, 1060}}};
    auto operation = next::internal::triangulation_operation_lease{};
    auto outer_status = next::TriangulateResult::fail;
    const auto& outer_graph =
        next::internal::execute_triangulation_graph(operation, outer_paths, false, outer_status);
    ASSERT_EQ(outer_status, next::TriangulateResult::success);
    const auto expected_outer = next::internal::build_triangulation_result(outer_graph);
    ASSERT_EQ(small_area2(expected_outer), 200);

    auto inner_status = next::TriangulateResult::fail;
    const auto inner_result =
        next::internal::execute_triangulation(inner_paths, false, inner_status);
    ASSERT_EQ(inner_status, next::TriangulateResult::success);
    EXPECT_EQ(small_area2(inner_result), 2400);
    EXPECT_EQ(next::internal::build_triangulation_result(outer_graph), expected_outer);
}

TEST(Clipper2NextTriangulationExecutorTests, ActiveReleaseWaitsForCompleteResultMaterialization) {
    next::release_thread_caches();
    const next::Paths64 paths{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    {
        auto operation = next::internal::triangulation_operation_lease{};
        auto status = next::TriangulateResult::fail;
        const auto& graph = next::internal::execute_triangulation_graph(operation, paths, true, status);
        ASSERT_EQ(status, next::TriangulateResult::success);
        const auto expected = next::internal::build_triangulation_result(graph);
        next::release_thread_caches();
        next::release_thread_caches();
        EXPECT_EQ(next::internal::build_triangulation_result(graph), expected);
        EXPECT_EQ(small_area2(expected), 200);
        ASSERT_EQ(operation.context().triangles.size(), graph.size());
        EXPECT_EQ(operation.context().triangles.front().get(), &graph.triangle(0));
    }
    auto reuse = next::internal::triangulation_operation_lease{};
    EXPECT_TRUE(reuse.context().triangles.empty());
    EXPECT_EQ(reuse.context().vertex_pool.retained_capacity(), 0U);
    EXPECT_EQ(reuse.context().vertices.capacity(), 0U);
}

TEST(Clipper2NextTriangulationExecutorTests, GraphPreparationOccursOncePerOperation) {
    next::release_thread_caches();
    auto operation = next::internal::triangulation_operation_lease{};
    auto& graph = operation.context();
    auto* vertex = next::internal::create_triangulation_vertex(graph, {1, 2});
    EXPECT_EQ(&operation.context(), &graph);
    ASSERT_EQ(operation.context().vertices.size(), 1U);
    EXPECT_EQ(operation.context().vertices.front(), vertex);
}

TEST(Clipper2NextTriangulationExecutorTests, ScopeUnwindAndPartialOwnerReleasePermitReuse) {
    for (unsigned mask = 0; mask != 4; ++mask) {
        SCOPED_TRACE(mask);
        next::release_thread_caches();
        try {
            auto operation = next::internal::triangulation_operation_lease{};
            if (mask & 1U) {
                static_cast<void>(next::internal::create_triangulation_vertex(operation.context(), {1, 2}));
            }
            if (mask & 2U) { operation.cache().entries.reserve(2); }
            next::release_thread_caches();
            next::release_thread_caches();
            throw mask;
        } catch (unsigned value) {
            EXPECT_EQ(value, mask);
        }
        auto reuse = next::internal::triangulation_operation_lease{};
        EXPECT_TRUE(reuse.context().vertices.empty());
        EXPECT_EQ(reuse.context().vertex_pool.retained_capacity(), 0U);
        EXPECT_EQ(reuse.cache().entries.capacity(), 0U);
        EXPECT_EQ(reuse.cache().total_units, 0U);
    }
}

TEST(Clipper2NextTriangulationExecutorTests, NestedCacheWorkAndDeferredReleasePreserveOuterCopies) {
    next::release_thread_caches();
    const auto request = cache_sized_request();
    {
        auto operation = next::internal::triangulation_operation_lease{};
        auto status = next::TriangulateResult::fail;
        const auto& graph =
            next::internal::execute_triangulation_graph(operation, request.paths, true, status);
        ASSERT_EQ(status, next::TriangulateResult::success);
        const auto expected = next::internal::build_triangulation_result(graph);
        ASSERT_EQ(expected.size(), 256U);
        ASSERT_EQ(small_area2(expected), 2560000);
        next::internal::store_cached_triangulation_graph(operation, request, status, graph);
        ASSERT_EQ(operation.cache().entries.size(), 1U);
        EXPECT_TRUE(operation.cache().entries.back().result.triangles.empty());

        const auto inner = next::triangulate_checked(cache_sized_request(1000000));
        ASSERT_TRUE(inner.has_value());
        ASSERT_EQ(inner->status, next::TriangulateResult::success);
        EXPECT_EQ(small_area2(inner->triangles), 2560000);
        for (const auto& triangle : inner->triangles) {
            for (const auto& point : triangle) { EXPECT_GE(point.x, 1000000); }
        }
        ASSERT_EQ(operation.cache().entries.size(), 1U);
        EXPECT_EQ(operation.cache().entries.front().request_paths, request.paths);
        next::release_thread_caches();
        next::release_thread_caches();

        next::triangulation_result64 copied;
        ASSERT_TRUE(next::internal::try_get_cached_triangulation(operation, request, copied));
        EXPECT_EQ(copied.status, next::TriangulateResult::success);
        EXPECT_EQ(copied.triangles, expected);
        next::triangulation_resultd scaled;
        ASSERT_TRUE(next::internal::try_get_cached_triangulation(operation, request, scaled, 0.01));
        ASSERT_EQ(scaled.triangles.size(), expected.size());
        for (std::size_t i = 0; i != expected.size(); ++i) {
            ASSERT_EQ(scaled.triangles[i].size(), expected[i].size());
            for (std::size_t j = 0; j != expected[i].size(); ++j) {
                EXPECT_EQ(scaled.triangles[i][j].x, expected[i][j].x * 0.01);
                EXPECT_EQ(scaled.triangles[i][j].y, expected[i][j].y * 0.01);
            }
        }
        EXPECT_EQ(scaled.status, next::TriangulateResult::success);
    }
    auto next_operation = next::internal::triangulation_operation_lease{};
    next::triangulation_result64 copied;
    EXPECT_FALSE(next::internal::try_get_cached_triangulation(next_operation, request, copied));
}

TEST(Clipper2NextTriangulationExecutorTests, GraphReusePreservesAnUncopiedCacheEntry) {
    next::release_thread_caches();
    const auto first = cache_sized_request();
    const auto second = cache_sized_request(1000000);
    const auto first_result = next::triangulate(first);
    const auto second_result = next::triangulate(second);
    ASSERT_EQ(first_result.status, next::TriangulateResult::success);
    ASSERT_EQ(second_result.status, next::TriangulateResult::success);

    auto operation = next::internal::triangulation_operation_lease{};
    ASSERT_EQ(operation.cache().entries.size(), 2U);
    EXPECT_TRUE(operation.cache().entries.back().result.triangles.empty());
    next::triangulation_result64 copied;
    ASSERT_TRUE(next::internal::try_get_cached_triangulation(operation, first, copied));
    EXPECT_EQ(copied.triangles, first_result.triangles);
    ASSERT_TRUE(next::internal::try_get_cached_triangulation(operation, second, copied));
    EXPECT_EQ(copied.triangles, second_result.triangles);
}

TEST(Clipper2NextTriangulationExecutorTests, CacheEligibilityKeepsPointBoundaryAndDelaunayMode) {
    for (const auto points : {511U, 512U}) {
        for (const bool delaunay : {false, true}) {
            SCOPED_TRACE(::testing::Message() << points << '/' << delaunay);
            next::release_thread_caches();
            auto request = cache_sized_request();
            request.use_delaunay = delaunay;
            if (points == 511U) { request.paths.back().pop_back(); }
            auto operation = next::internal::triangulation_operation_lease{};
            auto status = next::TriangulateResult::fail;
            const auto& graph = next::internal::execute_triangulation_graph(
                operation, request.paths, delaunay, status);
            ASSERT_EQ(status, next::TriangulateResult::success);
            const auto expected = next::internal::build_triangulation_result(graph);
            next::internal::store_cached_triangulation_graph(operation, request, status, graph);
            next::triangulation_result64 copied;
            const bool hit = next::internal::try_get_cached_triangulation(operation, request, copied);
            EXPECT_EQ(hit, points == 512U && delaunay);
            if (hit) {
                EXPECT_EQ(copied.triangles, expected);
                for (const bool change_x : {false, true}) {
                    auto changed = request;
                    auto& point = changed.paths.front().front();
                    if (change_x) { ++point.x; }
                    else { ++point.y; }
                    EXPECT_FALSE(next::internal::try_get_cached_triangulation(
                        operation, changed, copied));
                    EXPECT_EQ(copied.triangles, expected);
                }
                auto reordered = request;
                std::reverse(reordered.paths.begin(), reordered.paths.end());
                EXPECT_FALSE(next::internal::try_get_cached_triangulation(
                    operation, reordered, copied));
                EXPECT_EQ(copied.triangles, expected);
            }
        }
    }
}
