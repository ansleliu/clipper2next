#include <numbers>
#include <limits>

#include <gtest/gtest.h>

#include "clipper2next/core.h"
#include "clipper2next/geometry.h"
#include "geometry/private/geometry_predicates.h"
#include "geometry/private/numeric_policy.h"

namespace next = clipper2next;

TEST(Clipper2NextGeometryPredicatesTests, NumericPolicyDefinesPrecisionAndPiWithoutPublicMacros) {
    EXPECT_DOUBLE_EQ(next::internal::pi, std::numbers::pi_v<double>);
    EXPECT_EQ(next::internal::precision_limit, 8);
    EXPECT_EQ(next::internal::clamp_decimal_precision(42), 8);
    EXPECT_EQ(next::internal::clamp_decimal_precision(-42), -8);
    EXPECT_EQ(next::internal::clamp_decimal_precision(3), 3);
    const auto valid_precision = next::check_precision_range(3);
    ASSERT_TRUE(valid_precision.has_value());
    EXPECT_EQ(valid_precision.value(), 3);
    const auto invalid_precision = next::check_precision_range(42);
    ASSERT_FALSE(invalid_precision.has_value());
    EXPECT_EQ(invalid_precision.error(), next::clipper_error_code::precision_out_of_range);
}

TEST(Clipper2NextGeometryPredicatesTests, PredicatePolicySelectsIntersectionPrecision) {
    const next::predicate_policy default_policy;
    const next::predicate_policy precise_policy{next::precision_mode::precise};
    next::Point64 fast_intersection;
    next::Point64 precise_intersection;

    EXPECT_EQ(default_policy.mode, next::precision_mode::fast);
    ASSERT_TRUE(next::line_intersection_point(next::Point64{0, 0},
                                              next::Point64{10, 10},
                                              next::Point64{0, 10},
                                              next::Point64{10, 0},
                                              fast_intersection,
                                              default_policy));
    ASSERT_TRUE(next::line_intersection_point(next::Point64{0, 0},
                                              next::Point64{10, 10},
                                              next::Point64{0, 10},
                                              next::Point64{10, 0},
                                              precise_intersection,
                                              precise_policy));
    EXPECT_EQ(fast_intersection, next::Point64(5, 5));
    EXPECT_EQ(precise_intersection, next::Point64(5, 5));
}

TEST(Clipper2NextGeometryPredicatesTests, IntegralAreaRemainsStableForLargeTranslatedRings) {
    constexpr int64_t base = 1'000'000'000'000;
    const next::Path64 narrow_rectangle{
        {base, base},
        {base + 10, base},
        {base + 10, base + 7},
        {base, base + 7},
    };

    EXPECT_DOUBLE_EQ(next::area(narrow_rectangle), 70.0);
}

TEST(Clipper2NextGeometryPredicatesTests, PathsAreaRemainsStableForLargeTranslatedRings) {
    constexpr int64_t base = 1'000'000'000'000;
    const next::Paths64 rectangles{
        {
            {base, base},
            {base + 10, base},
            {base + 10, base + 7},
            {base, base + 7},
        },
        {
            {base + 100, base + 100},
            {base + 112, base + 100},
            {base + 112, base + 105},
            {base + 100, base + 105},
        },
    };

    EXPECT_DOUBLE_EQ(next::area(rectangles), 130.0);
}

TEST(Clipper2NextGeometryPredicatesTests, CrossProductSignClassifiesTurnsAndCollinearPoints) {
    EXPECT_GT(
        next::cross_product_sign(next::Point64{0, 0}, next::Point64{10, 0}, next::Point64{10, 10}),
        0);
    EXPECT_LT(
        next::cross_product_sign(next::Point64{0, 0}, next::Point64{10, 0}, next::Point64{10, -10}),
        0);
    EXPECT_EQ(
        next::cross_product_sign(next::Point64{0, 0}, next::Point64{10, 0}, next::Point64{20, 0}),
        0);
}

TEST(Clipper2NextGeometryPredicatesTests, TrustedRangePredicateMatchesExactBoundaryResults) {
    constexpr auto low = next::MIN_COORD;
    constexpr auto high = next::MAX_COORD;
    const next::Point64 first{low, low};
    const next::Point64 second{high, low};
    const next::Point64 left_turn{high, high};
    const next::Point64 collinear{0, low};

    EXPECT_EQ(next::internal::cross_product_sign_in_clipper_range(first, second, left_turn),
              next::cross_product_sign(first, second, left_turn));
    EXPECT_EQ(next::internal::cross_product_sign_in_clipper_range(first, second, collinear),
              next::cross_product_sign(first, second, collinear));

    const next::Point64 other_first{low, high};
    const next::Point64 other_second{high, low};
    EXPECT_EQ(next::internal::segments_properly_intersect_in_clipper_range(
                  first, left_turn, other_first, other_second),
              next::segments_intersect(first, left_turn, other_first, other_second));
}

TEST(Clipper2NextGeometryPredicatesTests, FloatingHelpersSubtractWideCoordinatesWithoutOverflow) {
    constexpr auto minimum = (std::numeric_limits<int64_t>::lowest)();
    constexpr auto maximum = (std::numeric_limits<int64_t>::max)();
    const auto span = static_cast<double>(maximum) - static_cast<double>(minimum);
    const next::Point64 low{minimum, minimum};
    const next::Point64 high{maximum, maximum};

    EXPECT_DOUBLE_EQ(next::distance_squared(low, high), 2.0 * span * span);
    EXPECT_DOUBLE_EQ(next::cross_product(low,
                                         next::Point64{maximum, minimum},
                                         high),
                     span * span);
    EXPECT_DOUBLE_EQ(next::perpendicular_distance_from_line_squared(
                         next::Point64{minimum, maximum}, low, high),
                     0.5 * span * span);
}

TEST(Clipper2NextGeometryPredicatesTests, PointInPolygonClassifiesInsideOutsideAndBoundary) {
    const next::Path64 square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    EXPECT_EQ(next::point_in_polygon(next::Point64{5, 5}, square),
              next::PointInPolygonResult::IsInside);
    EXPECT_EQ(next::point_in_polygon(next::Point64{20, 5}, square),
              next::PointInPolygonResult::IsOutside);
    EXPECT_EQ(next::point_in_polygon(next::Point64{10, 5}, square),
              next::PointInPolygonResult::IsOn);
}

TEST(Clipper2NextGeometryPredicatesTests, PointInPolygonReportsBoundaryAtVertices) {
    const next::Path64 square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    EXPECT_EQ(next::point_in_polygon(next::Point64{0, 0}, square),
              next::PointInPolygonResult::IsOn);
    EXPECT_EQ(next::point_in_polygon(next::Point64{10, 10}, square),
              next::PointInPolygonResult::IsOn);
}

TEST(Clipper2NextGeometryPredicatesTests, PointInPolygonHandlesConcaveWinding) {
    // Pentagon with an upward bump along the bottom edge, creating a notch.
    const next::Path64 chevron{{0, 0}, {4, 2}, {8, 0}, {8, 8}, {0, 8}};

    EXPECT_EQ(next::point_in_polygon(next::Point64{4, 1}, chevron),
              next::PointInPolygonResult::IsOutside);
    EXPECT_EQ(next::point_in_polygon(next::Point64{4, 5}, chevron),
              next::PointInPolygonResult::IsInside);
}

TEST(Clipper2NextGeometryPredicatesTests, PointInPolygonUsesExactArithmeticForLargeCoordinates) {
    // Coordinate products exceed 2^53, so a double-precision ray-intersection
    // rounds to the wrong side of the query point, while exact int128 cross
    // products classify correctly. The query point lies inside triangle ABC.
    const next::Path64 triangle{{-1046265977509, -111010324029},
                                {-509239871815, -269462436342},
                                {636303052087, -886262487984}};

    EXPECT_EQ(next::point_in_polygon(next::Point64{-599094829052, -242950302900}, triangle),
              next::PointInPolygonResult::IsInside);
}
