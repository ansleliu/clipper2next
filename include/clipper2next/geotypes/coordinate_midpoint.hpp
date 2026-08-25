#pragma once

#include <type_traits>

namespace geotypes {

template <typename T>
[[nodiscard]] constexpr auto midpointCoordinate(T first, T second) -> T {
    if constexpr (std::is_integral_v<T>) {
        const T quotient_sum = first / 2 + second / 2;
        const T remainder_sum = first % 2 + second % 2;
        if constexpr (std::is_signed_v<T>) {
            if (remainder_sum == T{2}) { return quotient_sum + T{1}; }
            if (remainder_sum == T{-2}) { return quotient_sum - T{1}; }
            if (remainder_sum == T{1} && quotient_sum < T{}) { return quotient_sum + T{1}; }
            if (remainder_sum == T{-1} && quotient_sum > T{}) { return quotient_sum - T{1}; }
            return quotient_sum;
        } else {
            return quotient_sum + remainder_sum / T{2};
        }
    } else {
        return (first + second) / 2;
    }
}

}  // namespace geotypes
