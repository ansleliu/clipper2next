#include <clipper2next/offset.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace next = clipper2next;

namespace {

auto square_request() -> next::offset_request64 {
    auto request = next::offset_request64{};
    request.paths = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    request.delta = 10.0;
    return request;
}

auto borrowed_result(const next::offset_request64& request)
    -> next::expected_borrowed_offset_stage_result64 {
    const auto group = next::borrowed_offset_group64{
        next::borrow_paths64(request.paths), request.join_type, request.end_type};
    auto borrowed = next::borrowed_offset_request64{};
    borrowed.groups = std::span{&group, 1U};
    borrowed.delta = request.delta;
    borrowed.miter_limit = request.miter_limit;
    borrowed.arc_tolerance = request.arc_tolerance;
    borrowed.arc_segments_per_quadrant = request.arc_segments_per_quadrant;
    borrowed.coordinate_rounding = request.coordinate_rounding;
    borrowed.options = request.options;
    return next::offset_stage_checked(borrowed);
}

void expect_rejected(const next::offset_request64& request) {
    EXPECT_NO_THROW({
        const auto owning = next::offset_checked(request);
        const auto borrowed = borrowed_result(request);
        ASSERT_FALSE(owning.has_value());
        ASSERT_FALSE(borrowed.has_value());
        EXPECT_EQ(owning.error(), next::clipper_error_code::invalid_argument);
        EXPECT_EQ(borrowed.error(), next::clipper_error_code::invalid_argument);
    });
}

}  // namespace

TEST(Clipper2NextOffsetErrorContractTests, NonFiniteParametersFailBeforeEmptyOrSmallDeltaReturn) {
    for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity()}) {
        for (const auto empty : {false, true}) {
            for (const auto delta : {0.0, 0.25, 10.0}) {
                for (const auto field : {0, 1, 2}) {
                    auto request = square_request();
                    if (empty) { request.paths.clear(); }
                    request.delta = delta;
                    if (field == 0) { request.delta = invalid; }
                    if (field == 1) { request.miter_limit = invalid; }
                    if (field == 2) { request.arc_tolerance = invalid; }
                    expect_rejected(request);
                }
            }
        }
    }
}

TEST(Clipper2NextOffsetErrorContractTests, InvalidEnumsAreRejectedInBothCheckedEntrypoints) {
    for (const auto empty : {false, true}) {
        for (const auto delta : {0.0, 10.0}) {
            for (const auto field : {0, 1, 2, 3}) {
                auto request = square_request();
                if (empty) { request.paths.clear(); }
                request.delta = delta;
                if (field == 0) {
                    request.coordinate_rounding = static_cast<geotypes::CoordinateRounding>(255);
                }
                if (field == 1) { request.join_type = static_cast<next::JoinType>(255); }
                if (field == 2) { request.end_type = static_cast<next::EndType>(255); }
                if (field == 3) {
                    request.options.intersection_policy.mode = static_cast<next::precision_mode>(255);
                }
                expect_rejected(request);
            }
        }
    }
}

TEST(Clipper2NextOffsetErrorContractTests, GeneratedCoordinateFailureReturnsRangeErrorWithoutThrowing) {
    auto request = square_request();
    const auto maximum = next::MAX_COORD;
    request.paths = {{{maximum - 4096, 0}, {maximum - 3072, 0},
                      {maximum - 3072, 1024}, {maximum - 4096, 1024}}};
    for (const auto delta : {8192.0, (std::numeric_limits<double>::max)()}) {
        request.delta = delta;
        EXPECT_NO_THROW({
            const auto owning = next::offset_checked(request);
            const auto borrowed = borrowed_result(request);
            ASSERT_FALSE(owning.has_value());
            ASSERT_FALSE(borrowed.has_value());
            EXPECT_EQ(owning.error(), next::clipper_error_code::coordinate_range);
            EXPECT_EQ(borrowed.error(), next::clipper_error_code::coordinate_range);
        });
    }
}

TEST(Clipper2NextOffsetErrorContractTests, ValidEmptyAndErosionRemainSuccessful) {
    auto request = square_request();
    request.delta = -100.0;
    auto owning = next::offset_checked(request);
    auto borrowed = borrowed_result(request);
    ASSERT_TRUE(owning);
    ASSERT_TRUE(borrowed);
    EXPECT_TRUE(owning->closed.empty());
    EXPECT_TRUE(borrowed->paths.empty());
    request.paths.clear();
    request.delta = 10.0;
    owning = next::offset_checked(request);
    borrowed = borrowed_result(request);
    ASSERT_TRUE(owning);
    ASSERT_TRUE(borrowed);
    EXPECT_TRUE(owning->closed.empty());
    EXPECT_TRUE(borrowed->paths.empty());
}

TEST(Clipper2NextOffsetErrorContractTests, SinglePointRoundReportsGeneratedCoordinateRange) {
    auto request = next::offset_request64{};
    request.paths = {{{0, 0}}};
    request.join_type = next::JoinType::Round;
    request.end_type = next::EndType::Round;
    request.arc_segments_per_quadrant = 8U;
    // The first radius fits int64 but exceeds the engine domain. The others
    // are rejected by the ellipse generator before it produces any vertices.
    for (const auto delta : {0x1p62, 0x1p63, -0x1p63,
                             (std::numeric_limits<double>::max)()}) {
        SCOPED_TRACE(delta);
        request.delta = delta;
        EXPECT_NO_THROW({
            const auto owning = next::offset_checked(request);
            const auto borrowed = borrowed_result(request);
            EXPECT_FALSE(owning.has_value());
            EXPECT_FALSE(borrowed.has_value());
            if (!owning) { EXPECT_EQ(owning.error(), next::clipper_error_code::coordinate_range); }
            if (!borrowed) { EXPECT_EQ(borrowed.error(), next::clipper_error_code::coordinate_range); }
        });
    }
}

TEST(Clipper2NextOffsetErrorContractTests, SinglePointRoundPreservesValidCircleAndSubunitEmptyResults) {
    auto request = next::offset_request64{};
    request.paths = {{{0, 0}}};
    request.join_type = next::JoinType::Round;
    request.end_type = next::EndType::Round;
    for (const auto segments : {0U, 8U}) {
        request.arc_segments_per_quadrant = segments;
        for (const auto delta : {0.75, 1.0, 4.0, -4.0}) {
            SCOPED_TRACE(delta);
            request.delta = delta;
            const auto owning = next::offset_checked(request);
            const auto borrowed = borrowed_result(request);
            ASSERT_TRUE(owning.has_value());
            ASSERT_TRUE(borrowed.has_value());
            if (delta == 0.75) {
                EXPECT_TRUE(owning->closed.empty());
                EXPECT_TRUE(borrowed->paths.empty());
            } else {
                ASSERT_EQ(owning->closed.size(), 1U);
                ASSERT_EQ(borrowed->paths.size(), 1U);
                EXPECT_FALSE(owning->closed.front().empty());
                EXPECT_FALSE(borrowed->paths[0].empty());
            }
        }
    }
}

TEST(Clipper2NextOffsetErrorContractTests, SmallDeltaPreservesEachEntrypointsPathConvention) {
    auto request = square_request();
    request.paths.front().push_back(request.paths.front().front());
    const auto infinity = std::numeric_limits<double>::infinity();
    for (const auto delta : {std::nextafter(-0.5, -infinity), -0.5,
                             std::nextafter(-0.5, 0.0), 0.0,
                             std::nextafter(0.5, 0.0), 0.5, std::nextafter(0.5, infinity)}) {
        request.delta = delta;
        const auto owning = next::offset_checked(request);
        const auto borrowed = borrowed_result(request);
        ASSERT_TRUE(owning);
        ASSERT_TRUE(borrowed);
        ASSERT_EQ(owning->closed.size(), 1U);
        ASSERT_EQ(borrowed->paths.size(), 1U);
        if (std::abs(delta) < 0.5) {
            EXPECT_EQ(owning->closed, request.paths);
        } else {
            EXPECT_EQ(owning->closed.front().size(), 4U);
        }
        EXPECT_EQ(borrowed->paths[0].size(), 4U);
    }
}

TEST(Clipper2NextOffsetErrorContractTests, FiniteAutomaticMiterAndArcSettingsRemainAccepted) {
    auto request = square_request();
    request.miter_limit = -1.0;
    request.arc_tolerance = -1.0;
    const auto owning = next::offset_checked(request);
    const auto borrowed = borrowed_result(request);
    ASSERT_TRUE(owning);
    ASSERT_TRUE(borrowed);
    EXPECT_FALSE(owning->closed.empty());
    EXPECT_FALSE(borrowed->paths.empty());
}
