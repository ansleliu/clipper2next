#include <gtest/gtest.h>

#include "path_equivalence.h"

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

TEST(Clipper2NextOraclePathEquivalenceTests, ClosedPathsIgnoreStartAndPathOrder) {
    const auto expected = legacy::Paths64{
        legacy::MakePath({0, 0, 10, 0, 10, 10, 0, 10}),
        legacy::MakePath({20, 20, 30, 20, 30, 30, 20, 30}),
    };
    const auto actual = next::Paths64{
        {{30, 30}, {20, 30}, {20, 20}, {30, 20}},
        {{10, 10}, {0, 10}, {0, 0}, {10, 0}},
    };

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextOraclePathEquivalenceTests, ClosedPathsRejectReversedWinding) {
    const auto expected = legacy::Paths64{
        legacy::MakePath({0, 0, 10, 0, 10, 10, 0, 10}),
    };
    const auto actual = next::Paths64{
        {{0, 0}, {0, 10}, {10, 10}, {10, 0}},
    };

    EXPECT_THROW(oracle::assert_paths_semantically_equal(expected, actual), std::runtime_error);
}

TEST(Clipper2NextOraclePathEquivalenceTests, ClosedPathsRejectDifferentGeometry) {
    const auto expected = legacy::Paths64{
        legacy::MakePath({0, 0, 10, 0, 10, 10, 0, 10}),
    };
    const auto actual = next::Paths64{
        {{0, 0}, {10, 0}, {11, 10}, {0, 10}},
    };

    EXPECT_THROW(oracle::assert_paths_semantically_equal(expected, actual), std::runtime_error);
}

TEST(Clipper2NextOraclePathEquivalenceTests, OpenPathsIgnoreIndependentPathOrder) {
    const auto expected = legacy::Paths64{
        legacy::MakePath({0, 0, 10, 0, 20, 10}),
        legacy::MakePath({30, 0, 40, 0, 50, 10}),
    };
    const auto actual = next::Paths64{
        {{30, 0}, {40, 0}, {50, 10}},
        {{0, 0}, {10, 0}, {20, 10}},
    };

    EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(expected, actual));
}

TEST(Clipper2NextOraclePathEquivalenceTests, OpenPathsRejectRotation) {
    const auto expected = legacy::Paths64{
        legacy::MakePath({0, 0, 10, 0, 20, 10}),
    };
    const auto actual = next::Paths64{
        {{10, 0}, {20, 10}, {0, 0}},
    };

    EXPECT_THROW(oracle::assert_open_paths_exactly_equal(expected, actual), std::runtime_error);
}

TEST(Clipper2NextOraclePathEquivalenceTests, OpenPathsRejectReversedDirection) {
    const auto expected = legacy::Paths64{
        legacy::MakePath({0, 0, 10, 0, 20, 10}),
    };
    const auto actual = next::Paths64{
        {{20, 10}, {10, 0}, {0, 0}},
    };

    EXPECT_THROW(oracle::assert_open_paths_exactly_equal(expected, actual), std::runtime_error);
}
