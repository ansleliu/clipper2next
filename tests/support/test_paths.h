// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/core.h"

#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace clipper2next::tests {

[[nodiscard]] inline auto path64(std::initializer_list<int64_t> coordinates) -> Path64 {
    if ((coordinates.size() % 2U) != 0U) {
        throw std::invalid_argument{"path64 requires an even coordinate count"};
    }

    Path64 result;
    result.reserve(coordinates.size() / 2U);
    auto iterator = coordinates.begin();
    while (iterator != coordinates.end()) {
        const auto x = *iterator;
        ++iterator;
        const auto y = *iterator;
        ++iterator;
        result.emplace_back(x, y);
    }
    return result;
}

[[nodiscard]] inline auto square64(int64_t left, int64_t top, int64_t right, int64_t bottom)
    -> Path64 {
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

}  // namespace clipper2next::tests
