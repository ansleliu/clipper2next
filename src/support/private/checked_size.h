#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace clipper2next::internal {

[[nodiscard]] inline auto checked_size_add(std::size_t left, std::size_t right)
    -> std::size_t {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (right > maximum - left) { throw std::length_error{"clipper2next capacity overflow"}; }
    return left + right;
}

[[nodiscard]] inline auto checked_size_multiply(std::size_t left, std::size_t right)
    -> std::size_t {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (left != 0U && right > maximum / left) {
        throw std::length_error{"clipper2next capacity overflow"};
    }
    return left * right;
}

}  // namespace clipper2next::internal
