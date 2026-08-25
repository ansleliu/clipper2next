#include "support/private/checked_size.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace next = clipper2next;

TEST(Clipper2NextCheckedSizeTests, RejectsCapacityArithmeticOverflow) {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();

    EXPECT_EQ(next::internal::checked_size_add(3U, 4U), 7U);
    EXPECT_EQ(next::internal::checked_size_multiply(3U, 4U), 12U);
    EXPECT_THROW(static_cast<void>(next::internal::checked_size_add(maximum, 1U)),
                 std::length_error);
    EXPECT_THROW(static_cast<void>(next::internal::checked_size_multiply(maximum, 2U)),
                 std::length_error);
}
