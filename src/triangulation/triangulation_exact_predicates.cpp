#include "triangulation/private/triangulation_exact_predicates.h"

#include "geometry/private/geometry_exact.h"
#include <cassert>

namespace clipper2next::internal {
namespace {

template <std::size_t N>
using words = std::array<std::uint64_t, N>;

auto magnitude(std::int64_t value) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(value < 0 ? -value : value);
}

template <std::size_t N>
auto compare(const words<N>& a, const words<N>& b) noexcept -> int {
    for (auto i = N; i-- > 0;) {
        if (a[i] != b[i]) { return a[i] > b[i] ? 1 : -1; }
    }
    return 0;
}

template <std::size_t N>
auto subtract(words<N> a, const words<N>& b) noexcept -> words<N> {
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const auto partial = a[i] - b[i];
        const auto first_borrow = a[i] < b[i];
        a[i] = partial - borrow;
        borrow = first_borrow || partial < borrow;
    }
    return a;
}

template <std::size_t A, std::size_t B>
auto multiply(const words<A>& a, const words<B>& b) noexcept -> words<A + B> {
    words<A + B> result{};
    for (std::size_t i = 0; i < A; ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < B; ++j) {
            // a*b + existing + carry <= (2^64-1)^2 + 2*(2^64-1) < 2^128.
            // Account for both low-word overflows before advancing the column.
            auto product = geometry_exact::unsigned_product(a[i], b[j]);
            const auto partial = product.lo + result[i + j];
            product.hi += partial < product.lo;
            const auto total = partial + carry;
            product.hi += total < partial;
            result[i + j] = total;
            carry = product.hi;
        }
        result[i + B] = carry;
    }
    return result;
}

template <std::size_t N>
struct signed_words {
    words<N> value{};
    int sign = 0;
};

template <std::size_t N>
auto add(signed_words<N> a, const signed_words<N>& b) noexcept -> signed_words<N> {
    if (a.sign == 0) { return b; }
    if (b.sign == 0) { return a; }
    if (a.sign == b.sign) {
        geometry_exact::add_words(a.value, b.value);
        return a;
    }
    const auto order = compare(a.value, b.value);
    if (order == 0) { return {}; }
    return order > 0 ? signed_words<N>{subtract(a.value, b.value), a.sign}
                     : signed_words<N>{subtract(b.value, a.value), b.sign};
}

auto product(std::int64_t a, std::int64_t b) noexcept -> signed_words<2> {
    const auto p = geometry_exact::unsigned_product(magnitude(a), magnitude(b));
    return {{p.lo, p.hi}, geometry_exact::product_sign(a, b)};
}

auto cross(std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by) noexcept
    -> signed_words<2> {
    auto second = product(ay, bx);
    second.sign = -second.sign;
    return add(product(ax, by), second);
}

auto squared_length(std::int64_t x, std::int64_t y) noexcept -> words<2> {
    const auto xx = geometry_exact::unsigned_product(magnitude(x), magnitude(x));
    const auto yy = geometry_exact::unsigned_product(magnitude(y), magnitude(y));
    auto result = words<2>{xx.lo, xx.hi};
    geometry_exact::add_words(result, words<2>{yy.lo, yy.hi});
    return result;
}


template <std::size_t N>
auto sign_of(const signed_words<N>& value) noexcept -> int {
    return compare(value.value, words<N>{}) == 0 ? 0 : value.sign;
}

auto oriented_in_circle_wide(std::int64_t ax, std::int64_t ay,
                             std::int64_t bx, std::int64_t by,
                             std::int64_t cx, std::int64_t cy) noexcept -> oriented_in_circle_result {
    // Admitted D < 2^62: orientation prefixes < 2^127, circle prefixes < 2^252.
    const auto bc = cross(bx, by, cx, cy);
    const auto ca = cross(cx, cy, ax, ay);
    const auto ab = cross(ax, ay, bx, by);
    auto determinant = signed_words<4>{multiply(squared_length(ax, ay), bc.value), bc.sign};
    determinant = add(determinant, {multiply(squared_length(bx, by), ca.value), ca.sign});
    determinant = add(determinant, {multiply(squared_length(cx, cy), ab.value), ab.sign});
    return {sign_of(determinant), sign_of(add(add(bc, ca), ab))};
}

auto oriented_in_circle_bounded(std::int64_t ax, std::int64_t ay,
                                std::int64_t bx, std::int64_t by,
                                std::int64_t cx, std::int64_t cy,
                                circle_precision precision) noexcept -> oriented_in_circle_result {
    // D < 2^30: every lift/minor < 2^61 and orientation prefixes < 2^63.
    const auto bc = bx * cy - by * cx, ca = cx * ay - cy * ax, ab = ax * by - ay * bx;
    const auto lift_a = ax * ax + ay * ay, lift_b = bx * bx + by * by, lift_c = cx * cx + cy * cy;
    const auto orientation = bc + ca + ab;
    const int orientation_sign = (orientation > 0) - (orientation < 0);
    if (precision == circle_precision::exact64) {
        // Existing exact64 domain: |circle| <= 12*D^4 < 2^60.
        const auto determinant = lift_a * bc + lift_b * ca + lift_c * ab;
        return {(determinant > 0) - (determinant < 0), orientation_sign};
    }
    // Exact128 domain: each product/prefix is bounded by 12*D^4 < 2^124.
    const auto determinant = add(add(product(lift_a, bc), product(lift_b, ca)), product(lift_c, ab));
    return {sign_of(determinant), orientation_sign};
}

}  // namespace

auto oriented_in_circle(const Point64& a, const Point64& b, const Point64& c,
                        const Point64& d, const triangulation_predicate_domain& domain) noexcept
    -> oriented_in_circle_result {
    assert(domain.contains(a) && domain.contains(b) && domain.contains(c) && domain.contains(d));
    const auto ax = a.x - d.x, ay = a.y - d.y;
    const auto bx = b.x - d.x, by = b.y - d.y;
    const auto cx = c.x - d.x, cy = c.y - d.y;
    if (domain.precision() != circle_precision::exact256) {
        return oriented_in_circle_bounded(ax, ay, bx, by, cx, cy, domain.precision());
    }
    return oriented_in_circle_wide(ax, ay, bx, by, cx, cy);
}

auto squared_point_distance_in_clipper_range(Point64 a, Point64 b) noexcept -> exact_point_distance {
    // Each admitted difference is < 2^62; the sum of two squares is < 2^125.
    return {squared_length(a.x - b.x, a.y - b.y)};
}

auto compare_squared_distance(const exact_point_distance& a,
                              const exact_point_distance& b) noexcept -> int {
    return compare(a.squared, b.squared);
}

}  // namespace clipper2next::internal
