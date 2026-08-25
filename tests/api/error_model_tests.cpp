#include "clipper2next/api/error.h"

#include <gtest/gtest.h>

#include <string>

namespace next = clipper2next;

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
