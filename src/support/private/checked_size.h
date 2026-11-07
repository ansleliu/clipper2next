#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace clipper2next::internal {

[[nodiscard]] inline auto checked_size_add(std::size_t left, std::size_t right)
    -> std::size_t {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (right > maximum - left) { throw std::length_error{"clipper2next capacity overflow"}; }
    return left + right;
}

[[nodiscard]] inline auto checked_size_multiply(std::size_t left, std::size_t right)
    -> std::size_t {
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned __int64 high{};
    const auto product = _umul128(left, right, &high);
    if (high != 0U) { throw std::length_error{"clipper2next capacity overflow"}; }
    return product;
#else
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (left != 0U && right > maximum / left) {
        throw std::length_error{"clipper2next capacity overflow"};
    }
    return left * right;
#endif
}

}  // namespace clipper2next::internal
