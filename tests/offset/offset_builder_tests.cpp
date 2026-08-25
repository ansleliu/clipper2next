#include "clipper2next/offset/builder.h"
#include "clipper2next/api/options.h"
#include "clipper2next/clipper.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetBuilderTests, NonFiniteOffsetParametersProduceNoGeometry) {
    const next::Paths64 point_path{next::Path64{{10, 10}}};
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();

    EXPECT_TRUE(next::offset_builder{}.delta(infinity).add(point_path).execute().empty());
    EXPECT_TRUE(next::offset_builder{}.delta(nan).add(point_path).execute().empty());
    EXPECT_TRUE(next::offset_builder{}
                    .delta(2.0)
                    .miter_limit(infinity)
                    .add(point_path)
                    .execute()
                    .empty());
    EXPECT_TRUE(next::offset_builder{}
                    .delta(2.0)
                    .arc_tolerance(nan)
                    .add(point_path)
                    .execute()
                    .empty());
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

TEST(Clipper2NextOffsetBuilderTests, UnrepresentableFiniteDeltaProducesNoGeometry) {
    const next::Path64 point_path{{10, 10}};

    EXPECT_TRUE(next::offset_builder{}
                    .delta((std::numeric_limits<double>::max)())
                    .join(next::JoinType::Miter)
                    .add(point_path)
                    .execute()
                    .empty());
}

TEST(Clipper2NextOffsetBuilderTests, NonFiniteDeltaCallbackCannotEscapeToCoordinates) {
    const next::Paths64 polygon{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };

    const auto result = next::offset_builder{}
                            .delta(2.0)
                            .delta_callback([](const auto&, const auto&, auto, auto) {
                                return std::numeric_limits<double>::quiet_NaN();
                            })
                            .add(polygon)
                            .execute();

    for (const auto& path : result) {
        for (const auto& point : path) {
            EXPECT_GE(point.x, next::MIN_COORD);
            EXPECT_LE(point.x, next::MAX_COORD);
            EXPECT_GE(point.y, next::MIN_COORD);
            EXPECT_LE(point.y, next::MAX_COORD);
        }
    }
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
