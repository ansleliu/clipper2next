#include "clipper2next/geometry.h"
#include "clipper2next/minkowski.h"
#include "minkowski/private/minkowski.h"
#include "support/test_paths.h"

#include <cstddef>
#include <limits>

#include <gtest/gtest.h>

namespace next = clipper2next;
namespace test = clipper2next::tests;

namespace {

}  // namespace

TEST(Clipper2NextMinkowskiTests, EmptyPatternOrPathReturnsEmptyResult) {
    next::minkowski_request64 request;
    request.pattern = test::path64({0, 0, 10, 0, 10, 10, 0, 10});

    EXPECT_TRUE(next::minkowski_sum(request).empty());

    request.pattern.clear();
    request.path = test::path64({100, 100, 120, 100, 120, 120, 100, 120});

    EXPECT_TRUE(next::minkowski_difference(request).empty());
}

TEST(Clipper2NextMinkowskiTests, ClosedSumExpandsByPatternBounds) {
    next::minkowski_request64 request;
    request.pattern = test::path64({0, 0, 10, 0, 10, 10, 0, 10});
    request.path = test::path64({100, 100, 120, 100, 120, 120, 100, 120});
    request.is_closed = true;

    const auto result = next::minkowski_sum(request);

    ASSERT_FALSE(result.empty());
    EXPECT_EQ(next::bounds(result), (next::Rect64{100, 100, 130, 130}));
    EXPECT_GT(next::area(result), 0.0);
}

TEST(Clipper2NextMinkowskiTests, ClosedDifferenceSubtractsPatternBounds) {
    next::minkowski_request64 request;
    request.pattern = test::path64({0, 0, 10, 0, 10, 10, 0, 10});
    request.path = test::path64({100, 100, 120, 100, 120, 120, 100, 120});
    request.is_closed = true;

    const auto result = next::minkowski_difference(request);

    ASSERT_FALSE(result.empty());
    EXPECT_EQ(next::bounds(result), (next::Rect64{90, 90, 120, 120}));
    EXPECT_GT(next::area(result), 0.0);
}

TEST(Clipper2NextMinkowskiTests, SinglePointPathReturnsEmptyIntegerResult) {
    next::minkowski_request64 request;
    request.pattern = test::path64({0, 0, 10, 0, 10, 10, 0, 10});
    request.path = test::path64({100, 200});
    request.is_closed = true;

    EXPECT_TRUE(next::minkowski_sum(request).empty());
    EXPECT_TRUE(next::minkowski_difference(request).empty());
}

TEST(Clipper2NextMinkowskiTests, SinglePointPathReturnsEmptyDoubleResult) {
    next::minkowski_requestd request;
    request.pattern = next::PathD{{0.0, 0.0}, {1.5, 0.0}, {1.5, 1.5}, {0.0, 1.5}};
    request.path = next::PathD{{10.0, 20.0}};
    request.is_closed = true;

    EXPECT_TRUE(next::minkowski_sum(request).empty());
    EXPECT_TRUE(next::minkowski_difference(request).empty());
}

TEST(Clipper2NextMinkowskiTests, OpenSumFollowsOpenPathSegmentsOnly) {
    next::minkowski_request64 request;
    request.pattern = test::path64({-5, -5, 5, -5, 5, 5, -5, 5});
    request.path = test::path64({0, 0, 20, 0, 20, 20});
    request.is_closed = false;

    const auto result = next::minkowski_sum(request);

    ASSERT_FALSE(result.empty());
    EXPECT_EQ(next::bounds(result), (next::Rect64{-5, -5, 25, 25}));
}

TEST(Clipper2NextMinkowskiTests, DoubleRequestsScaleAndUnscaleIntegerResult) {
    next::minkowski_requestd request;
    request.pattern = next::PathD{{0.0, 0.0}, {1.5, 0.0}, {1.5, 1.5}, {0.0, 1.5}};
    request.path = next::PathD{{10.0, 10.0}, {12.0, 10.0}, {12.0, 12.0}, {10.0, 12.0}};
    request.is_closed = true;
    request.decimal_precision = 2;

    const auto result = next::minkowski_sum(request);

    ASSERT_FALSE(result.empty());
    EXPECT_EQ(next::bounds(result), (next::RectD{10.0, 10.0, 13.5, 13.5}));
}

TEST(Clipper2NextMinkowskiTests, InvalidDoublePrecisionReturnsEmptyResult) {
    next::minkowski_requestd request;
    request.pattern = next::PathD{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    request.path = next::PathD{{10.0, 10.0}, {12.0, 10.0}, {12.0, 12.0}, {10.0, 12.0}};
    request.decimal_precision = 99;

    EXPECT_TRUE(next::minkowski_sum(request).empty());
    EXPECT_TRUE(next::minkowski_difference(request).empty());
}

TEST(Clipper2NextMinkowskiTests, UncheckedApisRejectOverflowingTranslations) {
    next::minkowski_request64 sum_request;
    sum_request.pattern = next::Path64{{1, 0}, {1, 1}};
    sum_request.path = next::Path64{{(std::numeric_limits<int64_t>::max)(), 0},
                                    {(std::numeric_limits<int64_t>::max)(), 1}};

    next::minkowski_request64 difference_request;
    difference_request.pattern = next::Path64{{1, 0}, {1, 1}};
    difference_request.path = next::Path64{{(std::numeric_limits<int64_t>::min)(), 0},
                                           {(std::numeric_limits<int64_t>::min)(), 1}};

    EXPECT_TRUE(next::minkowski_sum(sum_request).empty());
    EXPECT_TRUE(next::minkowski_difference(difference_request).empty());
}
