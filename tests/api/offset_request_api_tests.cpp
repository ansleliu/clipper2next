#include "clipper2next/geometry.h"
#include "clipper2next/offset.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <limits>

namespace next = clipper2next;
namespace test = clipper2next::tests;

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

TEST(Clipper2NextOffsetRequestApiTests, NonFiniteArcToleranceProducesNoGeometry) {
    next::offset_request64 request;
    request.paths = next::Paths64{test::path64({0, 0, 100, 0, 100, 100, 0, 100})};
    request.delta = 10.0;
    request.arc_tolerance = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(next::offset(request).closed.empty());

    request.arc_tolerance = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(next::offset(request).closed.empty());
}
