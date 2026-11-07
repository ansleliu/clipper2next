#include "clipper2next/geometry.h"
#include "clipper2next/offset.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <limits>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetRequestApiTests, CorrectionRoundSingletonUsesRequestedQuantization) {
    for (const auto rounding : {geotypes::CoordinateRounding::NearestEven,
                                geotypes::CoordinateRounding::NearestAwayFromZero}) {
        next::offset_request64 request;
        request.paths = {{{0, 0}}};
        request.delta = 2.5;
        request.join_type = next::JoinType::Round;
        request.arc_segments_per_quadrant = 1;
        request.coordinate_rounding = rounding;
        const auto result = next::offset(request).closed;
        ASSERT_EQ(result.size(), 1U);
        const int radius = rounding == geotypes::CoordinateRounding::NearestEven ? 2 : 3;
        const next::Path64 expected{{radius, 0}, {0, radius}, {-radius, 0}, {0, -radius}};
        EXPECT_EQ(result.front().size(), 4U);
        for (const auto& point : expected) {
            EXPECT_NE(std::ranges::find(result.front(), point), result.front().end());
        }
    }
}

TEST(Clipper2NextOffsetRequestApiTests, OffsetUsesExecutionOptions) {
    next::offset_request64 request;
    ASSERT_DOUBLE_EQ(request.arc_tolerance, 0.0);
    request.paths = next::Paths64{
        test::path64({0, 0, 100, 0, 100, 100, 0, 100}),
    };
    request.delta = 10.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;
    request.options.reverse_solution = true;

    const auto result = next::offset(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_LT(next::area(result.closed), 0.0);
}

TEST(Clipper2NextOffsetRequestApiTests, ZeroDeltaOffsetPreservesInputVertices) {
    next::offset_request64 request;
    request.paths = next::Paths64{
        test::path64({0, 0, 10, 0, 10, 0, 0, 10, 0, 0}),
    };
    request.delta = 0.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;

    const auto result = next::offset(request);

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed.front(), request.paths.front());
}

TEST(Clipper2NextOffsetRequestApiTests, NonFiniteArcToleranceThrowsBeforeEmptyOrShortcut) {
    for (const auto delta : {0.0, 10.0}) {
        for (const auto bad : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
            next::offset_request64 request;
            request.delta = delta;
            request.arc_tolerance = bad;
            for (bool empty : {true, false}) {
                if (!empty) { request.paths = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}}; }
                try {
                    static_cast<void>(next::offset(request));
                    ADD_FAILURE() << "invalid arc tolerance accepted";
                } catch (const next::clipper_error& error) {
                    EXPECT_EQ(error.code(), next::clipper_error_code::invalid_argument);
                }
            }
        }
    }
}
