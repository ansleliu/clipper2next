#include "geometry/private/geometry_predicates.h"

#include "geometry/private/geometry_exact.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace clipper2next::internal {

namespace {

template <typename T>
[[nodiscard]] auto between_closed(T value, T first, T second) -> bool {
    return value >= (std::min)(first, second) && value <= (std::max)(first, second);
}

template <typename T>
[[nodiscard]] auto point_on_segment(const Point<T>& point,
                                    const Point<T>& segment_start,
                                    const Point<T>& segment_end) -> bool {
    if (cross_product_sign(segment_start, segment_end, point) != 0) { return false; }
    return between_closed(point.x, segment_start.x, segment_end.x) &&
           between_closed(point.y, segment_start.y, segment_end.y);
}

inline constexpr uint64_t int64_mul_safe_bound = 3037000499ULL;

#if !((defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX)
[[nodiscard]] inline auto unsigned_abs_i64(int64_t value) -> uint64_t {
    if (value >= 0) { return static_cast<uint64_t>(value); }
    return static_cast<uint64_t>(-(value + 1)) + 1U;
}

[[nodiscard]] inline auto product_fits_int64(int64_t lhs, int64_t rhs) -> bool {
    const auto abs_lhs = unsigned_abs_i64(lhs);
    const auto abs_rhs = unsigned_abs_i64(rhs);
    return abs_lhs <= int64_mul_safe_bound && abs_rhs <= int64_mul_safe_bound;
}
#endif

struct signed_magnitude_difference {
    uint64_t magnitude = 0;
    int sign = 0;
};

[[nodiscard]] inline auto exact_difference(int64_t left, int64_t right)
    -> signed_magnitude_difference {
    if (left == right) { return {}; }
    if (left > right) {
        return {static_cast<uint64_t>(left) - static_cast<uint64_t>(right), 1};
    }
    return {static_cast<uint64_t>(right) - static_cast<uint64_t>(left), -1};
}

[[nodiscard]] inline auto compare_signed_products(const signed_magnitude_difference& first,
                                                  const signed_magnitude_difference& second,
                                                  const signed_magnitude_difference& third,
                                                  const signed_magnitude_difference& fourth)
    -> int {
    const auto first_sign = first.sign * second.sign;
    const auto second_sign = third.sign * fourth.sign;
    if (first_sign != second_sign) { return first_sign > second_sign ? 1 : -1; }
    if (first_sign == 0) { return 0; }

    const auto first_product =
        geometry_exact::unsigned_product(first.magnitude, second.magnitude);
    const auto second_product =
        geometry_exact::unsigned_product(third.magnitude, fourth.magnitude);
    const auto magnitude =
        geometry_exact::compare_unsigned_product(first_product, second_product);
    return first_sign > 0 ? magnitude : -magnitude;
}

}  // namespace

auto products_are_equal(int64_t a, int64_t b, int64_t c, int64_t d) -> bool {
#if (defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX
    const auto first_product = static_cast<__int128_t>(a) * static_cast<__int128_t>(b);
    const auto second_product = static_cast<__int128_t>(c) * static_cast<__int128_t>(d);
    return first_product == second_product;
#else
    if (product_fits_int64(a, b) && product_fits_int64(c, d)) { return a * b == c * d; }
    const auto first_abs = unsigned_abs_i64(a);
    const auto second_abs = unsigned_abs_i64(b);
    const auto third_abs = unsigned_abs_i64(c);
    const auto fourth_abs = unsigned_abs_i64(d);
    return geometry_exact::unsigned_product(first_abs, second_abs) ==
               geometry_exact::unsigned_product(third_abs, fourth_abs) &&
           geometry_exact::product_sign(a, b) == geometry_exact::product_sign(c, d);
#endif
}

auto cross_product_sign(const Point64& first, const Point64& second, const Point64& third) -> int {
#if (defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX
    int64_t ab_x = 0;
    int64_t bc_y = 0;
    int64_t ab_y = 0;
    int64_t bc_x = 0;
    if (!__builtin_sub_overflow(second.x, first.x, &ab_x) &&
        !__builtin_sub_overflow(third.y, second.y, &bc_y) &&
        !__builtin_sub_overflow(second.y, first.y, &ab_y) &&
        !__builtin_sub_overflow(third.x, second.x, &bc_x)) {
        const auto lhs = static_cast<__int128_t>(ab_x) * static_cast<__int128_t>(bc_y);
        const auto rhs = static_cast<__int128_t>(ab_y) * static_cast<__int128_t>(bc_x);
        return (lhs > rhs) - (lhs < rhs);
    }
#endif

    const auto exact_ab_x = exact_difference(second.x, first.x);
    const auto exact_bc_y = exact_difference(third.y, second.y);
    const auto exact_ab_y = exact_difference(second.y, first.y);
    const auto exact_bc_x = exact_difference(third.x, second.x);

    if (exact_ab_x.magnitude <= int64_mul_safe_bound &&
        exact_bc_y.magnitude <= int64_mul_safe_bound &&
        exact_ab_y.magnitude <= int64_mul_safe_bound &&
        exact_bc_x.magnitude <= int64_mul_safe_bound) {
        const auto lhs = static_cast<int64_t>(exact_ab_x.magnitude) *
                         static_cast<int64_t>(exact_bc_y.magnitude) * exact_ab_x.sign *
                         exact_bc_y.sign;
        const auto rhs = static_cast<int64_t>(exact_ab_y.magnitude) *
                         static_cast<int64_t>(exact_bc_x.magnitude) * exact_ab_y.sign *
                         exact_bc_x.sign;
        return (lhs > rhs) - (lhs < rhs);
    }
    return compare_signed_products(exact_ab_x, exact_bc_y, exact_ab_y, exact_bc_x);
}

auto cross_product_sign_in_clipper_range(const Point64& first,
                                         const Point64& second,
                                         const Point64& third) -> int {
    const auto ab_x = second.x - first.x;
    const auto bc_y = third.y - second.y;
    const auto ab_y = second.y - first.y;
    const auto bc_x = third.x - second.x;
#if (defined(__clang__) || defined(__GNUC__)) && UINTPTR_MAX >= UINT64_MAX
    const auto lhs = static_cast<__int128_t>(ab_x) * static_cast<__int128_t>(bc_y);
    const auto rhs = static_cast<__int128_t>(ab_y) * static_cast<__int128_t>(bc_x);
    return (lhs > rhs) - (lhs < rhs);
#elif defined(_MSC_VER) && defined(_M_X64)
    __int64 lhs_high = 0;
    __int64 rhs_high = 0;
    const auto lhs_low = static_cast<unsigned __int64>(_mul128(ab_x, bc_y, &lhs_high));
    const auto rhs_low = static_cast<unsigned __int64>(_mul128(ab_y, bc_x, &rhs_high));
    if (lhs_high != rhs_high) { return lhs_high > rhs_high ? 1 : -1; }
    return (lhs_low > rhs_low) - (lhs_low < rhs_low);
#else
    return cross_product_sign(first, second, third);
#endif
}

auto cross_product_sign(const PointD& first, const PointD& second, const PointD& third) -> int {
    const auto cross = cross_product(first, second, third);
    return (cross > 0.0) - (cross < 0.0);
}

auto segments_intersect(const Point64& first_start,
                        const Point64& first_end,
                        const Point64& second_start,
                        const Point64& second_end,
                        bool inclusive) -> bool {
    const auto o1 =
        clipper2next::internal::cross_product_sign(first_start, first_end, second_start);
    const auto o2 = clipper2next::internal::cross_product_sign(first_start, first_end, second_end);
    const auto o3 =
        clipper2next::internal::cross_product_sign(second_start, second_end, first_start);
    const auto o4 = clipper2next::internal::cross_product_sign(second_start, second_end, first_end);

    if (!inclusive) { return o1 * o2 < 0 && o3 * o4 < 0; }

    const auto first_collinear = o1 == 0 && o2 == 0;
    const auto second_collinear = o3 == 0 && o4 == 0;
    if (first_collinear || second_collinear) { return false; }
    if (o1 == 0 && point_on_segment(second_start, first_start, first_end)) { return true; }
    if (o2 == 0 && point_on_segment(second_end, first_start, first_end)) { return true; }
    if (o3 == 0 && point_on_segment(first_start, second_start, second_end)) { return true; }
    if (o4 == 0 && point_on_segment(first_end, second_start, second_end)) { return true; }
    return o1 * o2 < 0 && o3 * o4 < 0;
}

auto segments_properly_intersect_in_clipper_range(const Point64& first_start,
                                                  const Point64& first_end,
                                                  const Point64& second_start,
                                                  const Point64& second_end) -> bool {
    const auto o1 =
        cross_product_sign_in_clipper_range(first_start, first_end, second_start);
    const auto o2 = cross_product_sign_in_clipper_range(first_start, first_end, second_end);
    if (o1 * o2 >= 0) { return false; }
    const auto o3 =
        cross_product_sign_in_clipper_range(second_start, second_end, first_start);
    const auto o4 = cross_product_sign_in_clipper_range(second_start, second_end, first_end);
    return o3 * o4 < 0;
}

}  // namespace clipper2next::internal
