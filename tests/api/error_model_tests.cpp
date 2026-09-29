#include "clipper2next/api/error.h"

#include <gtest/gtest.h>

#include <string>

namespace next = clipper2next;

// Error integers are part of the published ABI. New values append to this list.
static_assert(static_cast<int>(next::clipper_error_code::ok) == 0);
static_assert(static_cast<int>(next::clipper_error_code::precision_out_of_range) == 1);
static_assert(static_cast<int>(next::clipper_error_code::scale_out_of_range) == 2);
static_assert(static_cast<int>(next::clipper_error_code::non_pair_input) == 3);
static_assert(static_cast<int>(next::clipper_error_code::coordinate_range) == 4);
static_assert(static_cast<int>(next::clipper_error_code::resource_limit) == 5);
static_assert(static_cast<int>(next::clipper_error_code::allocation_failure) == 6);
static_assert(static_cast<int>(next::clipper_error_code::input_access_failure) == 7);
static_assert(static_cast<int>(next::clipper_error_code::input_changed) == 8);
static_assert(static_cast<int>(next::clipper_error_code::executor_failure) == 9);
static_assert(static_cast<int>(next::clipper_error_code::sink_failure) == 10);
static_assert(static_cast<int>(next::clipper_error_code::internal_error) == 11);

TEST(Clipper2NextErrorModelTests, InvalidArgumentIsReportedWithoutRenumberingPublishedErrors) {
    const next::clipper_error error{next::clipper_error_code::invalid_argument};
    EXPECT_EQ(error.code(), next::clipper_error_code::invalid_argument);
    EXPECT_STREQ(error.what(), "Invalid request argument");
}

TEST(Clipper2NextErrorModelTests, ErrorCodeMapsToStableMessage) {
    const next::clipper_error error{next::clipper_error_code::precision_out_of_range};

    EXPECT_EQ(error.code(), next::clipper_error_code::precision_out_of_range);
    EXPECT_STREQ(error.what(), "Precision exceeds the permitted range");
}

TEST(Clipper2NextErrorModelTests, ClipperResultCanHoldValue) {
    next::clipper_result<int> result{42};

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(Clipper2NextErrorModelTests, ClipperResultSupportsExpectedLikeValueAccess) {
    next::clipper_result<int> result{42};

    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 42);
}

TEST(Clipper2NextErrorModelTests, ClipperResultSupportsExpectedLikePointerAccess) {
    next::clipper_result<std::string> result{std::string{"clipper"}};

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 7U);
}

TEST(Clipper2NextErrorModelTests, ClipperResultValueOrReturnsFallbackOnError) {
    const auto result = next::make_clipper_error<int>(next::clipper_error_code::coordinate_range);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.value_or(7), 7);
}
