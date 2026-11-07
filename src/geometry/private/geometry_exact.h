#pragma once

#include "clipper2next/geometry/math.h"

#include <array>
#include <cstdint>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#elif defined(__x86_64__)
#include <immintrin.h>
#endif

namespace clipper2next::internal::geometry_exact {

[[nodiscard]] inline auto unsigned_product(uint64_t first, uint64_t second) -> UInt128Struct {
#if (defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX
    const auto product = static_cast<__uint128_t>(first) * static_cast<__uint128_t>(second);
    return {static_cast<uint64_t>(product), static_cast<uint64_t>(product >> 64U)};
#elif defined(_MSC_VER) && defined(_M_X64)
    unsigned __int64 high = 0;
    const auto low = _umul128(first, second, &high);
    return {low, high};
#else
    return multiply_uint64(first, second);
#endif
}

[[nodiscard]] inline auto product_sign(int64_t first, int64_t second) -> int {
    return tri_sign(first) * tri_sign(second);
}

[[nodiscard]] inline auto compare_unsigned_product(const UInt128Struct& first,
                                                   const UInt128Struct& second) -> int {
    if (first.hi != second.hi) { return first.hi > second.hi ? 1 : -1; }
    if (first.lo == second.lo) { return 0; }
    return first.lo > second.lo ? 1 : -1;
}

// Each complete int64 product fits signed128. Compare products directly.
[[nodiscard]] inline auto compare_signed_products(int64_t a, int64_t b,
                                                  int64_t c, int64_t d) noexcept -> int {
#if (defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX
    const auto first = static_cast<__int128_t>(a) * static_cast<__int128_t>(b);
    const auto second = static_cast<__int128_t>(c) * static_cast<__int128_t>(d);
    return (first > second) - (first < second);
#elif defined(_MSC_VER) && defined(_M_X64)
    __int64 first_high = 0, second_high = 0;
    const auto first_low = static_cast<uint64_t>(_mul128(a, b, &first_high));
    const auto second_low = static_cast<uint64_t>(_mul128(c, d, &second_high));
    if (first_high != second_high) { return first_high > second_high ? 1 : -1; }
    return (first_low > second_low) - (first_low < second_low);
#else
    const auto first_sign = product_sign(a, b), second_sign = product_sign(c, d);
    if (first_sign != second_sign) { return first_sign > second_sign ? 1 : -1; }
    if (!first_sign) { return 0; }
    const auto magnitude = [](int64_t value) {
        return value < 0 ? uint64_t{0} - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    };
    return first_sign * compare_unsigned_product(unsigned_product(magnitude(a), magnitude(b)),
                                                 unsigned_product(magnitude(c), magnitude(d)));
#endif
}

// Addition modulo 2^(64*N), shared by the bounded geometry accumulators.
template <std::size_t N>
inline auto add_words(std::array<std::uint64_t, N>& sum,
                      const std::array<std::uint64_t, N>& value) noexcept -> void {
#if defined(_M_X64) || defined(__x86_64__)
    auto carry = static_cast<unsigned char>(0);
    for (std::size_t index = 0; index < N; ++index) {
        unsigned long long next{};
        carry = _addcarry_u64(carry, static_cast<unsigned long long>(sum[index]),
                             static_cast<unsigned long long>(value[index]), &next);
        sum[index] = static_cast<std::uint64_t>(next);
    }
#else
    auto carry = std::uint64_t{};
    for (std::size_t index = 0; index < N; ++index) {
        const auto partial = sum[index] + value[index];
        const auto first_carry = partial < sum[index];
        sum[index] = partial + carry;
        carry = first_carry || sum[index] < partial;
    }
#endif
}

}  // namespace clipper2next::internal::geometry_exact
