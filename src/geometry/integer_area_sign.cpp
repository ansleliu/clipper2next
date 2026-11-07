#include "geometry/private/integer_area_sign.h"

#include "geometry/private/geometry_exact.h"

#include <array>
#include <limits>

namespace clipper2next::internal {
namespace {

// Every int64 product lies in [-2^126 + 2^63, 2^126]. Their
// difference fits signed128, and fewer than 2^64 edges have partial sums
// strictly inside signed192. Uncentered products avoid coordinate subtraction.
static_assert(std::numeric_limits<std::size_t>::digits <= 64);

auto signed_product_bits(std::int64_t first, std::int64_t second) noexcept {
#if (defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX
    const auto product = static_cast<__int128_t>(first) * static_cast<__int128_t>(second);
    const auto bits = static_cast<__uint128_t>(product);
    return UInt128Struct{static_cast<std::uint64_t>(bits),
                         static_cast<std::uint64_t>(bits >> 64U)};
#elif defined(_MSC_VER) && defined(_M_X64)
    __int64 high = 0;
    const auto low = _mul128(first, second, &high);
    return UInt128Struct{static_cast<std::uint64_t>(low), static_cast<std::uint64_t>(high)};
#else
    const auto a = static_cast<std::uint64_t>(first);
    const auto b = static_cast<std::uint64_t>(second);
    auto result = geometry_exact::unsigned_product(a, b);
    // Correct unsigned operand representations modulo 2^128; the low word
    // is unchanged. This is defined even at INT64_MIN.
    result.hi -= (std::uint64_t{} - static_cast<std::uint64_t>(first < 0)) & b;
    result.hi -= (std::uint64_t{} - static_cast<std::uint64_t>(second < 0)) & a;
    return result;
#endif
}

auto add_edge(std::array<std::uint64_t, 3>& sum,
              const Point64& previous, const Point64& current) noexcept -> void {
    const auto first = signed_product_bits(previous.x, current.y);
    const auto second = signed_product_bits(previous.y, current.x);
    const auto low = first.lo - second.lo;
    const auto high = first.hi - second.hi - static_cast<std::uint64_t>(first.lo < second.lo);
    geometry_exact::add_words(sum, std::array<std::uint64_t, 3>{
        low, high, std::uint64_t{} - (high >> 63U)});
}

}  // namespace

auto signed_area_sign(const std::span<const Point64> path) noexcept -> int {
    if (path.size() < 3U) { return 0; }
    auto sum = std::array<std::uint64_t, 3>{};
    auto previous = path.back();
    for (const auto& current : path) {
        add_edge(sum, previous, current);
        previous = current;
    }
    if (sum[0] == 0 && sum[1] == 0 && sum[2] == 0) { return 0; }
    return (sum[2] >> 63U) != 0 ? -1 : 1;
}

}  // namespace clipper2next::internal
