#include "support/private/checked_size.h"

#include <gtest/gtest.h>

#include <array>
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

    constexpr auto half_word = std::size_t{1} << (std::numeric_limits<std::size_t>::digits / 2);
    const std::array values{std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3},
                            half_word - 1U, half_word, half_word + 1U,
                            maximum / 2U, maximum / 2U + 1U, maximum - 1U, maximum};
    for (const auto left : values) {
        for (const auto right : values) {
            if (left != 0U && right > maximum / left) {
                EXPECT_THROW(static_cast<void>(next::internal::checked_size_multiply(left, right)),
                             std::length_error);
            } else {
                EXPECT_EQ(next::internal::checked_size_multiply(left, right), left * right);
            }
        }
    }
}
