#include "clipper2next/geometry/path_transforms.h"
#include "clipper2next/geometry/translate.h"
#include "clipper2next/geometry/scaling.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace next = clipper2next;

TEST(Clipper2NextScalingErrorTests, InvalidPrecisionReturnsTypedError) {
    const auto precision = next::check_precision_range(99);

    ASSERT_FALSE(precision.has_value());
    EXPECT_EQ(precision.error(), next::clipper_error_code::precision_out_of_range);
}

TEST(Clipper2NextScalingErrorTests, ZeroScaleReturnsTypedError) {
    const next::PathD path{{0.0, 0.0}, {1.0, 1.0}};
    next::scale_request request;
    request.x = 0.0;

    const auto scaled = next::scale_path<int64_t>(path, request);

    ASSERT_FALSE(scaled.has_value());
    EXPECT_EQ(scaled.error(), next::clipper_error_code::scale_out_of_range);
}

TEST(Clipper2NextScalingErrorTests, OutOfRangeScaledBoundsReturnTypedError) {
    const next::PathsD paths{
        next::PathD{
            {next::max_coord, 0.0}, {next::max_coord, 1.0}, {next::max_coord - 1024.0, 1.0}},
    };

    const auto scaled = next::scale_paths<int64_t>(paths, next::scale_request{2.0, 1.0});

    ASSERT_FALSE(scaled.has_value());
    EXPECT_EQ(scaled.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextScalingErrorTests, AsymmetricScaleRoundTripPreservesRepresentableCoordinates) {
    const next::PathD path{{1.25, -2.5}, {10.5, 4.0}, {25.75, 12.5}};

    const auto scaled = next::scale_path<int64_t>(path, next::scale_request{4.0, 2.0});
    ASSERT_TRUE(scaled.has_value());
    const auto round_trip =
        next::scale_path<double>(scaled.value(), next::scale_request{0.25, 0.5});
    ASSERT_TRUE(round_trip.has_value());

    ASSERT_EQ(round_trip.value().size(), path.size());
    for (std::size_t index = 0; index < path.size(); ++index) {
        EXPECT_NEAR(round_trip.value()[index].x, path[index].x, 0.001);
        EXPECT_NEAR(round_trip.value()[index].y, path[index].y, 0.001);
    }
}

TEST(Clipper2NextScalingErrorTests, CoordinateBoundaryAtMaxCoordIsAccepted) {
    const next::Path64 path{{next::MAX_COORD - 4096, next::MAX_COORD - 8192},
                            {next::MAX_COORD, next::MAX_COORD - 8192},
                            {next::MAX_COORD, next::MAX_COORD}};

    const auto scaled = next::scale_path<int64_t>(path, next::scale_request{1.0, 1.0});

    ASSERT_TRUE(scaled.has_value());
    EXPECT_EQ(scaled.value(), path);
}

TEST(Clipper2NextScalingErrorTests, FloatingBoundaryThatRoundsPastMaxCoordReturnsTypedError) {
    const auto edge = next::max_coord / 2.0;
    const next::PathD path{{edge, edge}, {edge - 1024.0, edge}, {edge, edge - 1024.0}};

    const auto scaled = next::scale_path<int64_t>(path, next::scale_request{2.0, 2.0});

    ASSERT_FALSE(scaled.has_value());
    EXPECT_EQ(scaled.error(), next::clipper_error_code::coordinate_range);
}

TEST(Clipper2NextScalingErrorTests, TranslateInversePreservesLargeSafeCoordinates) {
    const next::Path64 path{{next::MAX_COORD - 4096, next::MIN_COORD + 8192},
                            {next::MAX_COORD - 2048, next::MIN_COORD + 8192},
                            {next::MAX_COORD - 2048, next::MIN_COORD + 12000}};

    const auto translated = next::translate(path, int64_t{-1024}, int64_t{2048});
    const auto round_trip = next::translate(translated, int64_t{1024}, int64_t{-2048});

    EXPECT_EQ(round_trip, path);
}

TEST(Clipper2NextScalingErrorTests, TransformPathRoundTripPreservesIntegralCoordinates) {
    const next::Path64 path{{-10, 20}, {30, -40}, {5000, 7000}};

    const auto as_double = next::transform_path<double>(path);
    const auto as_integer = next::transform_path<int64_t>(as_double);

    EXPECT_EQ(as_integer, path);
}

TEST(Clipper2NextScalingErrorTests, PrecisionSweepRoundTripsRepresentableDecimalCoordinates) {
    for (int index = 0; index < 64; ++index) {
        const auto x = static_cast<double>(index * 7) + 0.25;
        const auto y = static_cast<double>(index * -11) - 0.5;
        const next::PathD path{{x, y}, {x + 10.5, y}, {x + 10.5, y + 20.25}, {x, y + 20.25}};

        const auto scaled = next::scale_path<int64_t>(path, next::scale_request{4.0, 4.0});
        ASSERT_TRUE(scaled.has_value()) << index;
        const auto round_trip =
            next::scale_path<double>(scaled.value(), next::scale_request{0.25, 0.25});
        ASSERT_TRUE(round_trip.has_value()) << index;

        ASSERT_EQ(round_trip.value().size(), path.size()) << index;
        for (std::size_t point_index = 0; point_index < path.size(); ++point_index) {
            EXPECT_NEAR(round_trip.value()[point_index].x, path[point_index].x, 0.001) << index;
            EXPECT_NEAR(round_trip.value()[point_index].y, path[point_index].y, 0.001) << index;
        }
    }
}

TEST(Clipper2NextScalingErrorTests, TransformPathsRoundTripPreservesLargeMixedSigns) {
    constexpr int64_t exact_double_integer = 1'000'000'000'000;
    const next::Paths64 paths{
        {{-10'000'000, 20'000'000}, {30'000'000, -40'000'000}, {50'000'000, 70'000'000}},
        {{-exact_double_integer, exact_double_integer},
         {-exact_double_integer + 4096, exact_double_integer},
         {-exact_double_integer + 4096, exact_double_integer - 4096}}};

    const auto as_double = next::transform_paths<double>(paths);
    const auto as_integer = next::transform_paths<int64_t>(as_double);

    EXPECT_EQ(as_integer, paths);
}
