#include "clipper2next/offset/builder.h"
#include "clipper2next/api/options.h"
#include "clipper2next/clipper.h"
#include "clipper2next/geometry/algorithms.h"
#include "support/test_paths.h"
#include "support/path_equivalence.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetBuilderTests, CorrectionDegenerateGroupCannotChangeOperationDelta) {
    const next::Path64 square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    for (const bool degenerate_first : {false, true}) {
        auto builder = next::offset_builder{};
        builder.delta(-10).join(next::JoinType::Miter);
        if (degenerate_first) { builder.add(next::Path64{{300, 300}}); }
        builder.add(square);
        if (!degenerate_first) { builder.add(next::Path64{{300, 300}}); }
        EXPECT_DOUBLE_EQ(next::area(builder.execute()), 6400);
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionMixedGroupsShareWindingBasis) {
    const next::Path64 ccw{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    for (const bool clockwise : {false, true}) {
        auto square = ccw;
        if (clockwise) { std::reverse(square.begin(), square.end()); }
        for (const bool line_first : {false, true}) {
            auto builder = next::offset_builder{};
            builder.delta(10).join(next::JoinType::Miter);
            if (line_first) { builder.end(next::EndType::Butt).add(next::Path64{{200, 0}, {300, 0}}); }
            builder.end(next::EndType::Polygon).add(square);
            if (!line_first) { builder.end(next::EndType::Butt).add(next::Path64{{200, 0}, {300, 0}}); }
            const auto result = builder.execute();
            ASSERT_EQ(result.size(), 2U);
            EXPECT_DOUBLE_EQ(next::area(result), clockwise ? -16400 : 16400);
            EXPECT_TRUE(std::ranges::any_of(result, [](const auto& path) {
                return std::abs(next::area(path)) == 2000;
            }));
        }
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionDirectAndCleanupHaveSameShellDirection) {
    for (const bool clockwise : {false, true}) {
        for (const bool reverse : {false, true}) {
            for (const bool preserve : {false, true}) {
                next::Path64 square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
                if (clockwise) { std::reverse(square.begin(), square.end()); }
                const auto result = next::offset_builder{}.delta(10).join(next::JoinType::Miter)
                    .preserve_collinear(preserve).reverse_solution(reverse).add(square).execute();
                EXPECT_DOUBLE_EQ(next::area(result), clockwise != reverse ? -14400 : 14400);
            }
        }
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionTreeKeepsShellHoleIslandOwners) {
    const next::Paths64 rings{
        {{0, 0}, {200, 0}, {200, 200}, {0, 200}},
        {{50, 50}, {50, 150}, {150, 150}, {150, 50}},
        {{80, 80}, {120, 80}, {120, 120}, {80, 120}}};
    for (const double delta : {0.0, 10.0}) {
        for (const bool reverse_group : {false, true}) {
            for (const bool reverse_solution : {false, true}) {
                auto input = rings;
                if (reverse_group) { for (auto& ring : input) { std::ranges::reverse(ring); } }
                next::PolyTree64 tree;
                next::offset_builder{}.delta(delta).join(next::JoinType::Miter)
                    .reverse_solution(reverse_solution).add(input).execute_into(tree);
                ASSERT_EQ(tree.count(), 1U);
                const auto shell = tree.child(tree.root(), 0);
                ASSERT_EQ(tree.count(shell), 1U);
                const auto hole = tree.child(shell, 0);
                EXPECT_TRUE(tree.is_hole(hole));
                ASSERT_EQ(tree.count(hole), 1U);
                const auto island = tree.child(hole, 0);
                EXPECT_FALSE(tree.is_hole(island));
                EXPECT_EQ(tree.parent(island), hole);
                const double direction = reverse_group != reverse_solution ? -1 : 1;
                EXPECT_DOUBLE_EQ(tree.area(), direction * (delta == 0 ? 31600 : 45600));
                EXPECT_DOUBLE_EQ(next::area(tree.polygon(shell)), direction * (delta == 0 ? 40000 : 48400));
                EXPECT_DOUBLE_EQ(next::area(tree.polygon(hole)), -direction * (delta == 0 ? 10000 : 6400));
            }
        }
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionNonFiniteCallbackFailsAndClearsOutput) {
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        next::Paths64 output{{{7, 9}}};
        auto builder = next::offset_builder{}.delta(10).add(next::Path64{{0, 0}, {100, 0}, {100, 100}});
        builder.delta_callback([bad](const auto&, const auto&, auto, auto) { return bad; });
        try {
            builder.execute_into(output);
            FAIL() << "non-finite callback must fail";
        } catch (const next::clipper_error& error) {
            EXPECT_EQ(error.code(), next::clipper_error_code::invalid_argument);
        }
        EXPECT_TRUE(output.empty());
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionSingletonCallbackGetsEmptyNormalsAndEffectiveArc) {
    const next::Paths64 paths{{{0, 0}, {100, 0}, {100, 100}}, {{300, 0}}};
    auto builder = next::offset_builder{}.delta(1).join(next::JoinType::Round).add(paths);
    builder.delta_callback([](const auto& path, const auto& normals, auto current, auto previous) {
        if (path.size() == 1) {
            EXPECT_TRUE(normals.empty());
            EXPECT_EQ(current, 0U);
            EXPECT_EQ(previous, 0U);
        }
        return 100.0;
    });
    const auto result = builder.execute();
    const auto expected = next::offset_builder{}.delta(100).join(next::JoinType::Round).add(paths).execute();
    EXPECT_EQ(result, expected);
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionJoinedEndTypeDoesNotLeakBetweenPaths) {
    const next::Path64 line{{300, 0}, {400, 0}};
    const next::Path64 triangle{{0, 0}, {100, 0}, {100, 100}};
    const auto first = next::offset_builder{}.delta(10).join(next::JoinType::Bevel)
        .end(next::EndType::Joined).add(next::Paths64{line, triangle}).execute();
    const auto second = next::offset_builder{}.delta(10).join(next::JoinType::Bevel)
        .end(next::EndType::Joined).add(next::Paths64{triangle, line}).execute();
    EXPECT_DOUBLE_EQ(next::area(first), next::area(second));
    EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(first, second));
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionOppositePolygonGroupsUnionOverlapsOnce) {
    const next::Path64 positive{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const next::Path64 negative{{50, 100}, {150, 100}, {150, 0}, {50, 0}};
    for (const bool clockwise_first : {false, true}) {
        auto builder = next::offset_builder{}.delta(10).join(next::JoinType::Miter);
        builder.add(clockwise_first ? negative : positive).add(clockwise_first ? positive : negative);
        const auto result = builder.execute();
        next::Paths64 expected{{{-10, -10}, {160, -10}, {160, 110}, {-10, 110}}};
        if (clockwise_first) { std::ranges::reverse(expected.front()); }
        EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(expected, result));
        EXPECT_DOUBLE_EQ(next::area(result), clockwise_first ? -20400 : 20400);
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionRoundAndJoinedGroupsKeepBothComponents) {
    const next::Path64 clockwise{{0, 100}, {100, 100}, {100, 0}, {0, 0}};
    const next::Path64 line{{200, 0}, {300, 0}, {300, 100}};
    for (const auto end : {next::EndType::Round, next::EndType::Joined}) {
        for (const bool line_first : {false, true}) {
            auto builder = next::offset_builder{}.delta(10).join(next::JoinType::Round);
            if (line_first) { builder.end(end).add(line); }
            builder.end(next::EndType::Polygon).add(clockwise);
            if (!line_first) { builder.end(end).add(line); }
            auto expected = next::offset_builder{}.delta(10).join(next::JoinType::Round).add(clockwise).execute();
            auto line_result = next::offset_builder{}.delta(10).join(next::JoinType::Round).end(end).add(line).execute();
            ASSERT_GT(next::area(line_result), 0);
            for (auto& path : line_result) {
                std::ranges::reverse(path);
                expected.emplace_back(std::move(path));
            }
            // Whole-operation cleanup may remove collinear raw vertices retained
            // by a standalone direct result; compare their geometric boundaries.
            for (auto& path : expected) { path = next::trim_collinear(path); }
            EXPECT_NO_THROW(next::tests::oracle::assert_paths_semantically_equal(expected, builder.execute()));
        }
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionSignedJoinedCallbackRetainsNativeFill) {
    const next::Path64 square{{200, 0}, {300, 0}, {300, 100}, {200, 100}};
    const next::Path64 clockwise{{0, 100}, {100, 100}, {100, 0}, {0, 0}};
    for (const double callback : {-10.0, 10.0}) {
        for (const bool reverse_path : {false, true}) {
            for (const bool include_polygon : {false, true}) {
                auto path = square;
                if (reverse_path) { std::ranges::reverse(path); }
                auto builder = next::offset_builder{};
                builder.delta(10).join(next::JoinType::Miter).delta_callback(
                    [callback](const auto& points, const auto&, auto, auto) {
                        return points.front().x < 200 ? 10.0 : callback;
                    });
                if (include_polygon) { builder.end(next::EndType::Polygon).add(clockwise); }
                const auto actual = builder.end(next::EndType::Joined).add(path).execute();
                const auto joined_area = callback < 0 ? 0.0 : 8000.0;
                EXPECT_DOUBLE_EQ(next::area(actual), include_polygon ? -14400 - joined_area : joined_area);
            }
        }
    }
}

TEST(Clipper2NextOffsetBuilderTests, CorrectionRoundCallbackRebuildsSingletonArc) {
    const next::Path64 point{{0, 0}};
    const auto actual = next::offset_builder{}.delta(1).join(next::JoinType::Round).add(point)
        .delta_callback([](const auto&, const auto&, auto, auto) { return 100.0; }).execute();
    const auto expected = next::offset_builder{}.delta(100).join(next::JoinType::Round).add(point).execute();
    EXPECT_EQ(actual, expected);
    ASSERT_EQ(actual.size(), 1U);
    EXPECT_GT(actual.front().size(), 32U);
    for (const auto& vertex : actual.front()) {
        EXPECT_LE(std::abs(std::hypot(static_cast<double>(vertex.x), static_cast<double>(vertex.y)) - 100), std::sqrt(0.5));
    }
}

TEST(Clipper2NextOffsetBuilderTests, InvalidParametersThrowBeforeEmptyOrGenerationAndClearOutput) {
    const auto infinity = std::numeric_limits<double>::infinity();
    for (bool empty : {false, true}) {
        for (int invalid = 0; invalid != 6; ++invalid) {
            auto builder = next::offset_builder{};
            builder.delta(2.0);
            if (invalid == 0) { builder.delta(infinity); }
            if (invalid == 1) { builder.miter_limit(infinity); }
            if (invalid == 2) { builder.arc_tolerance(infinity); }
            if (invalid == 3) { builder.join(static_cast<next::JoinType>(-1)); }
            if (invalid == 4) { builder.end(static_cast<next::EndType>(-1)); }
            if (invalid == 5) {
                auto options = next::execution_options{};
                options.intersection_policy.mode = static_cast<next::precision_mode>(-1);
                builder.options(options);
            }
            // Empty groups still retain their own type contract.
            builder.add(empty ? next::Path64{} : next::Path64{{10, 10}});
            next::Paths64 output{{{99, 99}}};
            try {
                builder.execute_into(output);
                ADD_FAILURE() << "invalid parameters accepted: " << invalid;
            } catch (const next::clipper_error& error) {
                EXPECT_EQ(error.code(), next::clipper_error_code::invalid_argument);
            }
            EXPECT_TRUE(output.empty());
        }
    }
}

TEST(Clipper2NextOffsetBuilderTests, RoundPointHonorsLegacyArcTolerance) {
    const next::Path64 point_path{{0, 0}};
    for (const auto arc_tolerance : {0.0, 0.25, 2.0}) {
        const auto result = next::offset_builder{}
                                .delta(1'000'000.0)
                                .join(next::JoinType::Round)
                                .end(next::EndType::Polygon)
                                .arc_tolerance(arc_tolerance)
                                .add(point_path)
                                .execute();

        ASSERT_EQ(result.size(), 1U) << arc_tolerance;
        EXPECT_FALSE(result.front().empty()) << arc_tolerance;
    }
}

TEST(Clipper2NextOffsetBuilderTests, UnrepresentableFiniteDeltaReportsCoordinateRange) {
    const next::Path64 point_path{{10, 10}};
    try {
        static_cast<void>(next::offset_builder{}
                              .delta((std::numeric_limits<double>::max)())
                              .join(next::JoinType::Miter)
                              .add(point_path)
                              .execute());
        FAIL() << "generated coordinates must fit the engine domain";
    } catch (const next::clipper_error& error) {
        EXPECT_EQ(error.code(), next::clipper_error_code::coordinate_range);
    }
}

TEST(Clipper2NextOffsetBuilderTests, GeneratedRangeFailureDoesNotLeavePartialOutput) {
    const next::Path64 square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const next::Path64 outgrowing_point{{next::MAX_COORD - 1024, 0}};
    const auto builder = next::offset_builder{}.delta(8192).add(square).add(outgrowing_point);
    next::Paths64 output;
    EXPECT_THROW(builder.execute_into(output), next::clipper_error);
    EXPECT_TRUE(output.empty());
    next::PolyTree64 tree;
    EXPECT_THROW(builder.execute_into(tree), next::clipper_error);
    EXPECT_EQ(tree.count(), 0U);
}


TEST(Clipper2NextOffsetBuilderTests, FluentBuilderExecutesPolygonOffset) {
    const next::Path64 square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    const auto result = next::offset_builder{}
                            .delta(2.0)
                            .join(next::JoinType::Miter)
                            .end(next::EndType::Polygon)
                            .add(square)
                            .execute();

    EXPECT_FALSE(result.empty());
}

TEST(Clipper2NextOffsetBuilderTests, AppliesExecutionOptions) {
    next::execution_options options;
    options.reverse_solution = true;
    const auto paths = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::offset_builder{}
                            .options(options)
                            .delta(10.0)
                            .join(next::JoinType::Miter)
                            .end(next::EndType::Polygon)
                            .add(paths)
                            .execute();

    ASSERT_EQ(result.size(), 1U);
    EXPECT_LT(next::area(result), 0.0);
}

TEST(Clipper2NextOffsetBuilderTests, ExecutesWithDefaultOptions) {
    const auto paths = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };
    const auto result = next::offset_builder{}
                            .delta(10.0)
                            .join(next::JoinType::Miter)
                            .end(next::EndType::Polygon)
                            .add(paths)
                            .execute();

    ASSERT_EQ(result.size(), 1U);
    EXPECT_GT(std::abs(next::area(result)), 0.0);
}

TEST(Clipper2NextOffsetBuilderTests, PropagatesExecutionOptionsToOrientation) {
    next::execution_options options;
    options.reverse_solution = true;
    const auto paths = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };
    const auto result = next::offset_builder{}
                            .options(options)
                            .delta(10.0)
                            .join(next::JoinType::Miter)
                            .end(next::EndType::Polygon)
                            .add(paths)
                            .execute();

    ASSERT_EQ(result.size(), 1U);
    EXPECT_LT(next::area(result), 0.0);
}

TEST(Clipper2NextOffsetBuilderTests, LinksThroughProductPipeline) {
    const auto paths = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };

    const auto result = next::offset_builder{}
                            .delta(10.0)
                            .join(next::JoinType::Miter)
                            .end(next::EndType::Polygon)
                            .add(paths)
                            .execute();

    ASSERT_EQ(result.size(), 1U);
    EXPECT_GT(std::abs(next::area(result)), 0.0);
}

TEST(Clipper2NextOffsetBuilderTests, ExecuteCanBeRepeatedWithoutMutatingInputs) {
    const next::Path64 square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    auto builder = next::offset_builder{}
                       .delta(2.0)
                       .join(next::JoinType::Miter)
                       .end(next::EndType::Polygon)
                       .add(square);

    const auto first = builder.execute();
    const auto second = builder.execute();

    EXPECT_EQ(first, second);
}

TEST(Clipper2NextOffsetBuilderTests, ExecuteIsReentrantFromDeltaCallback) {
    const next::Path64 open_path{{0, 0}, {40, 0}, {80, 40}, {120, 40}};
    const auto expected = next::offset_builder{}
                              .end(next::EndType::Round)
                              .delta_callback([](const next::Path64&,
                                                 const next::PathD&,
                                                 std::size_t,
                                                 std::size_t) { return 8.0; })
                              .add(open_path)
                              .execute();

    next::offset_builder* builder_ptr = nullptr;
    bool nested_execution_started = false;
    auto builder = next::offset_builder{}
                       .end(next::EndType::Round)
                       .delta_callback([&](const next::Path64&,
                                           const next::PathD&,
                                           std::size_t,
                                           std::size_t) {
                           if (!nested_execution_started) {
                               nested_execution_started = true;
                               const auto nested = builder_ptr->execute();
                               EXPECT_EQ(nested, expected);
                           }
                           return 8.0;
                       })
                       .add(open_path);
    builder_ptr = &builder;

    EXPECT_EQ(builder.execute(), expected);
}

TEST(Clipper2NextOffsetBuilderTests, DeltaCallbackRunsWhenBaseDeltaIsZero) {
    const next::Path64 open_path{{0, 0}, {40, 0}, {80, 40}, {120, 40}};

    const auto result =
        next::offset_builder{}
            .join(next::JoinType::Miter)
            .end(next::EndType::Round)
            .delta_callback([](const next::Path64&,
                               const next::PathD&,
                               std::size_t current_index,
                               std::size_t) { return 5.0 + static_cast<double>(current_index); })
            .add(open_path)
            .execute();

    ASSERT_FALSE(result.empty());
    EXPECT_GT(next::area(result), 0.0);
}

TEST(Clipper2NextOffsetBuilderTests, CopyKeepsRequestDataWithoutSharingScratchState) {
    const next::Path64 square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const auto builder = next::offset_builder{}
                             .delta(2.0)
                             .join(next::JoinType::Miter)
                             .end(next::EndType::Polygon)
                             .add(square);

    auto copy = builder;
    const auto first = builder.execute();
    const auto copied = copy.execute();

    EXPECT_EQ(first, copied);
}

TEST(Clipper2NextOffsetBuilderTests, ExecutesIntoPolyTree) {
    const next::Path64 square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    next::PolyTree64 tree;

    next::offset_builder{}
        .delta(2.0)
        .join(next::JoinType::Miter)
        .end(next::EndType::Polygon)
        .add(square)
        .execute_into(tree);

    EXPECT_GT(tree.count(), 0U);
}

TEST(Clipper2NextOffsetBuilderTests, DirectMaterializationReusesFiveLevelUnionTree) {
    next::Paths64 source;
    for (int depth = 0; depth < 5; ++depth) {
        const auto lo = depth * 20;
        const auto hi = 240 - depth * 20;
        next::Path64 path{{lo, lo}, {hi, lo}, {hi, hi}, {lo, hi}};
        if (depth % 2) { std::reverse(path.begin(), path.end()); }
        source.push_back(std::move(path));
    }
    for (const double delta : {0.0, 2.0}) {
        next::offset_builder builder;
        builder.add(source).delta(delta).join(next::JoinType::Miter);
        next::PolyTree64 tree;
        builder.execute_into(tree);
        const auto expected = tree;
        std::vector<const next::Point64*> points;
        std::vector<const next::PolyTree64::node_id*> children;
        auto current = tree.root();
        for (unsigned depth = 0; depth <= 5; ++depth) {
            ASSERT_EQ(tree.depth(current), depth);
            ASSERT_EQ(tree.count(current), depth < 5 ? 1U : 0U);
            points.push_back(tree.polygon(current).data());
            children.push_back(tree.children(current).data());
            if (depth < 5) { current = tree.child(current, 0); }
        }
        builder.execute_into(tree);
        for (std::size_t i = 0; i < 6; ++i) {
            const auto id = next::PolyTree64::node_id{i};
            EXPECT_EQ(tree.polygon(id), expected.polygon(id));
            EXPECT_EQ(tree.parent(id), expected.parent(id));
            EXPECT_EQ(tree.depth(id), expected.depth(id));
            EXPECT_EQ(tree.is_hole(id), expected.is_hole(id));
            EXPECT_EQ(tree.polygon(id).data(), points[i]);
            EXPECT_EQ(tree.children(id).data(), children[i]);
        }
    }
}

TEST(Clipper2NextOffsetBuilderTests, ZeroVertexDeltaCleansRetracedBoundary) {
    for (const std::int64_t scale : {1LL,100LL}) {
        for (const std::int64_t translation : {0LL,1000003LL}) {
            for (const bool clockwise : {false,true}) {
                for (const bool reverse_output : {false,true}) {
                    next::Path64 path{{translation,translation+scale},
                        {translation+scale,translation},
                        {translation+scale,translation+2*scale},
                        {translation+scale,translation+scale},
                        {translation+2*scale,translation+scale}};
                    if (clockwise) { std::reverse(path.begin(),path.end()); }
                    // Both retraced branches have zero winding. The independent
                    // region is the triangle; no boundary may retain a spike.
                    next::Path64 triangle{{translation+scale,translation+scale},
                        {translation,translation+scale},
                        {translation+scale,translation}};
                    // Match the measured legacy order: reversing the contour
                    // reverses its starting point too. Its established cleanup
                    // also removes this unit-scale very small triangle.
                    if (clockwise != reverse_output) { std::reverse(triangle.begin(),triangle.end()); }
                    const auto actual = next::offset_builder{}.delta(1.0).join(next::JoinType::Miter)
                        .end(next::EndType::Polygon).reverse_solution(reverse_output)
                        .delta_callback([](const next::Path64&,const next::PathD&,
                                           std::size_t,std::size_t) { return 0.0; })
                        .add(path).execute();
                    EXPECT_EQ(actual,scale == 1 ? next::Paths64{} : next::Paths64{triangle});
                }
            }
        }
    }
}
