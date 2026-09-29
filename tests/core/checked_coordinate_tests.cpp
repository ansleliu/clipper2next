#include <clipper2next/geotypes/checked_coordinate.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace geo = geotypes;

namespace {
template <typename T>
void expectCoordinateError(const std::expected<T, geo::CoordinateError>& result,
                           geo::CoordinateError error) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error);
}
}  // namespace

TEST(Clipper2NextCheckedCoordinateTests, RejectsNonFiniteAndInvalidRoundingIncludingIdentityCasts) {
    for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        expectCoordinateError(geo::checkedCoordinateCast<std::int64_t>(value), geo::CoordinateError::NonFinite);
        expectCoordinateError(geo::checkedCoordinateCast<double>(value), geo::CoordinateError::NonFinite);
    }
    const auto invalid = static_cast<geo::CoordinateRounding>(255);
    expectCoordinateError(geo::checkedCoordinateCast<int>(0.5, invalid), geo::CoordinateError::InvalidRounding);
    expectCoordinateError(geo::checkedCoordinateCast<int>(1, invalid), geo::CoordinateError::InvalidRounding);
}

TEST(Clipper2NextCheckedCoordinateTests, RoundsBeforeRangeChecksAndHonorsBothTiePolicies) {
    EXPECT_EQ(geo::checkedCoordinateCast<std::uint8_t>(-0.25).value(), 0);
    EXPECT_EQ(geo::checkedCoordinateCast<std::int8_t>(127.25).value(), 127);
    expectCoordinateError(geo::checkedCoordinateCast<std::int8_t>(127.5), geo::CoordinateError::OutOfRange);
    EXPECT_EQ(geo::checkedCoordinateCast<int>(2.5).value(), 2);
    EXPECT_EQ(geo::checkedCoordinateCast<int>(1.5).value(), 2);
    EXPECT_EQ(geo::checkedCoordinateCast<int>(3.5).value(), 4);
    EXPECT_EQ(geo::checkedCoordinateCast<int>(-2.5).value(), -2);
    EXPECT_EQ(geo::checkedCoordinateCast<int>(2.5, geo::CoordinateRounding::NearestAwayFromZero).value(), 3);
    EXPECT_EQ(geo::checkedCoordinateCast<int>(-2.5, geo::CoordinateRounding::NearestAwayFromZero).value(), -3);
}

TEST(Clipper2NextCheckedCoordinateTests, HandlesInt64HalfOpenBoundsAcrossFloatingEnvironments) {
    const auto original = std::fegetround();
    ASSERT_NE(original, -1);
    for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        EXPECT_EQ(std::fesetround(mode), 0);
        EXPECT_EQ(geo::checkedCoordinateCast<std::int64_t>(-0x1p63).value(),
                  (std::numeric_limits<std::int64_t>::min)());
        expectCoordinateError(geo::checkedCoordinateCast<std::int64_t>(0x1p63), geo::CoordinateError::OutOfRange);
        const auto upper = std::nextafter(0x1p63, 0.0);
        EXPECT_EQ(geo::checkedCoordinateCast<std::int64_t>(upper).value(), static_cast<std::int64_t>(upper));
        EXPECT_FALSE(geo::checkedCoordinateCast<std::int64_t>(std::nextafter(-0x1p63, -INFINITY)));
        EXPECT_EQ(geo::checkedCoordinateCast<int>(2.5).value(), 2);
    }
    EXPECT_EQ(std::fesetround(original), 0);
    expectCoordinateError(geo::checkedCoordinateCast<std::uint64_t>(0x1p64), geo::CoordinateError::OutOfRange);
    EXPECT_EQ(geo::checkedCoordinateCast<std::int64_t>(static_cast<long double>(-0x1p63)).value(),
              (std::numeric_limits<std::int64_t>::min)());
    if constexpr (std::numeric_limits<long double>::digits >= 64) {
        EXPECT_EQ(geo::checkedCoordinateCast<std::int64_t>(
                      static_cast<long double>((std::numeric_limits<std::int64_t>::max)())).value(),
                  (std::numeric_limits<std::int64_t>::max)());
    }
}

TEST(Clipper2NextCheckedCoordinateTests, ChecksIntegralAndFloatingNarrowingWithoutSaturation) {
    expectCoordinateError(geo::checkedCoordinateCast<unsigned>(-1), geo::CoordinateError::OutOfRange);
    expectCoordinateError(geo::checkedCoordinateCast<std::int64_t>((std::numeric_limits<std::uint64_t>::max)()), geo::CoordinateError::OutOfRange);
    const auto floatMax = static_cast<double>((std::numeric_limits<float>::max)());
    EXPECT_EQ(geo::checkedCoordinateCast<float>(floatMax).value(), (std::numeric_limits<float>::max)());
    expectCoordinateError(geo::checkedCoordinateCast<float>(std::nextafter(floatMax, INFINITY)), geo::CoordinateError::OutOfRange);
    expectCoordinateError(geo::checkedCoordinateCast<float>(-std::numeric_limits<double>::max()), geo::CoordinateError::OutOfRange);
    EXPECT_TRUE(geo::checkedCoordinateCast<double>((std::numeric_limits<std::int64_t>::max)()));
}

TEST(Clipper2NextCheckedCoordinateTests, IntegralArithmeticChecksBeforeOverflowAndPreservesLargeIntegers) {
    constexpr auto low = (std::numeric_limits<std::int64_t>::min)();
    constexpr auto high = (std::numeric_limits<std::int64_t>::max)();
    EXPECT_FALSE(geo::checkedAdd(high, std::int64_t{1}));
    EXPECT_FALSE(geo::checkedSubtract(low, std::int64_t{1}));
    EXPECT_FALSE(geo::checkedNegate(low));
    EXPECT_FALSE(geo::checkedMultiply(low, std::int64_t{-1}));
    EXPECT_EQ(geo::checkedMultiply(low, std::int64_t{0}).value(), 0);
    EXPECT_EQ(geo::checkedMultiply(std::int64_t{-7}, std::int64_t{-3}).value(), 21);
    EXPECT_EQ(geo::checkedMultiply(std::int64_t{(std::int64_t{1} << 53) + 1}, std::int64_t{1}).value(),
              (std::int64_t{1} << 53) + 1);
    EXPECT_FALSE(geo::checkedSubtract(0U, 1U));
    EXPECT_FALSE(geo::checkedNegate(1U));
    EXPECT_FALSE(geo::checkedMultiply((std::numeric_limits<unsigned>::max)(), 2U));
    const auto source = geo::Point2i64{7, high};
    EXPECT_FALSE(geo::checkedAdd(source, geo::Point2i64{2, 1}));
    EXPECT_EQ(source, (geo::Point2i64{7, high}));
    EXPECT_EQ(geo::checkedMultiply(geo::Point2i64{-7, 3}, std::int64_t{-2}).value(),
              (geo::Point2i64{14, -6}));
    EXPECT_FALSE(geo::checkedPointCast<std::int64_t>(geo::Point2d{1.0, INFINITY}));
    EXPECT_EQ(geo::checkedPointCast<int>(geo::Point2d{1.5, -3.5}).value(),
              (geo::Point2<int>{2, -4}));
}

TEST(Clipper2NextCheckedCoordinateTests, SmallIntegerArithmeticMatchesIndependentWideResults) {
    for (int a = -128; a <= 127; ++a) {
        for (int b = -128; b <= 127; ++b) {
            const auto left = static_cast<std::int8_t>(a);
            const auto right = static_cast<std::int8_t>(b);
            const auto results = std::array{geo::checkedAdd(left, right),
                                             geo::checkedSubtract(left, right),
                                             geo::checkedMultiply(left, right)};
            const auto expected = std::array{a + b, a - b, a * b};
            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto fits = expected[i] >= -128 && expected[i] <= 127;
                ASSERT_EQ(results[i].has_value(), fits);
                if (fits) { EXPECT_EQ(results[i].value(), expected[i]); }
            }
        }
    }
}
