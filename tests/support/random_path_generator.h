// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/clipper.h"

#include <algorithm>
#include <cstdint>
#include <random>

namespace clipper2next::tests::support {

class random_path_generator final {
public:
    explicit random_path_generator(std::uint32_t seed)
        : rng_(seed) {}

    [[nodiscard]] auto integer(int min_value, int max_value) -> int {
        if (min_value == max_value) { return min_value; }
        std::uniform_int_distribution<int> distribution(min_value, max_value);
        return distribution(rng_);
    }

    [[nodiscard]] auto paths(int min_path_count, int max_complexity) -> Paths64 {
        max_complexity = std::max(1, max_complexity);
        std::uniform_int_distribution<int> first_coordinate(-max_complexity, max_complexity * 2);
        std::uniform_int_distribution<int> step(-5, 5);

        const auto path_count = integer(min_path_count, max_complexity);
        Paths64 result(static_cast<std::size_t>(path_count));
        for (auto& path : result) {
            const auto point_count = integer(0, std::max(0, max_complexity));
            path.reserve(static_cast<std::size_t>(point_count));
            for (int point = 0; point < point_count; ++point) {
                if (path.empty()) {
                    path.push_back(Point64{first_coordinate(rng_), first_coordinate(rng_)});
                } else {
                    const auto previous = path.back();
                    path.push_back(Point64{previous.x + step(rng_), previous.y + step(rng_)});
                }
            }
        }
        return result;
    }

    [[nodiscard]] auto clip_type() -> ClipType { return static_cast<ClipType>(integer(0, 4)); }

    [[nodiscard]] auto fill_rule() -> FillRule { return static_cast<FillRule>(integer(0, 3)); }

private:
    std::default_random_engine rng_;
};

}  // namespace clipper2next::tests::support
