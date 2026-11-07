// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/geometry/math.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <ranges>
#if (defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))) || (defined(_MSC_VER) && (defined(_M_AMD64) || defined(_M_X64)))
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif


namespace clipper2next {

namespace path_transform_detail {
#if (defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))) || (defined(_MSC_VER) && (defined(_M_AMD64) || defined(_M_X64)))
static_assert(sizeof(PointD) == 16 && sizeof(Point64) == 16);
static_assert(std::is_trivially_copyable_v<PointD>);
static_assert(std::is_standard_layout_v<PointD> && offsetof(PointD, x) == 0 && offsetof(PointD, y) == 8);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse4.1")))
#endif
inline auto floating_points_sse41(const PointD* source, Point64* target, std::size_t count) noexcept -> void {
    const auto high_signless = _mm_set1_epi32(0x7fffffff);
    const auto last_in_range = _mm_set1_epi32(0x43dfffff);
    const auto half_high = _mm_set1_epi32(0x3fe00000);
    const auto sign = _mm_castsi128_pd(_mm_set1_epi64x((std::numeric_limits<int64_t>::min)()));
    const auto magnitude = _mm_castsi128_pd(_mm_set1_epi64x((std::numeric_limits<int64_t>::max)()));
    const auto half = _mm_set1_pd(0.5);
    const auto one = _mm_set1_pd(1.0);
    for (std::size_t index = 0; index < count; ++index) {
        __m128d input;
        std::memcpy(&input, source + index, sizeof(input));
        const auto bits = _mm_castpd_si128(input);
        const auto high = _mm_and_si128(
            _mm_shuffle_epi32(bits, _MM_SHUFFLE(3, 3, 1, 1)), high_signless);
        if (_mm_movemask_epi8(_mm_cmpgt_epi32(high, last_in_range)) != 0) {
            target[index].x = geotypes::coordinateCast<int64_t>(
                source[index].x, geotypes::CoordinateRounding::NearestAwayFromZero);
            target[index].y = geotypes::coordinateCast<int64_t>(
                source[index].y, geotypes::CoordinateRounding::NearestAwayFromZero);
            continue;
        }
        // Values below half round to integer zero. Mask before FP operations
        // so denormals do not introduce a new arithmetic exception state.
        const auto tiny = _mm_cmplt_epi32(high, half_high);
        input = _mm_andnot_pd(_mm_castsi128_pd(tiny), input);
        const auto truncated = _mm_round_pd(input, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
        const auto fraction = _mm_and_pd(_mm_sub_pd(input, truncated), magnitude);
        const auto increment = _mm_and_pd(_mm_cmpge_pd(fraction, half), one);
        const auto signed_increment = _mm_or_pd(increment, _mm_and_pd(input, sign));
        const auto rounded = _mm_add_pd(truncated, signed_increment);
        target[index] = {_mm_cvttsd_si64(rounded),
                         _mm_cvttsd_si64(_mm_unpackhi_pd(rounded, rounded))};
    }
}
#endif

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
inline auto fill_floating_points_nonempty(
    const PointD* source, Point64* target, std::size_t count) -> void {
#if (defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))) || (defined(_MSC_VER) && (defined(_M_AMD64) || defined(_M_X64)))
#if defined(__GNUC__) || defined(__clang__)
    const auto available = __builtin_cpu_supports("sse4.1");
#else
    static const auto available = [] { int registers[4]; __cpuid(registers, 1); return (registers[2] & (1 << 19)) != 0; }();
#endif
    if (available) {
        floating_points_sse41(source, target, count);
        return;
    }
#endif
    for (std::size_t index = 0; index < count; ++index) {
        target[index].x = geotypes::coordinateCast<int64_t>(
            source[index].x, geotypes::CoordinateRounding::NearestAwayFromZero);
        target[index].y = geotypes::coordinateCast<int64_t>(
            source[index].y, geotypes::CoordinateRounding::NearestAwayFromZero);
    }
}

} // namespace path_transform_detail


template <typename T1, typename T2>
[[nodiscard]] inline auto transform_path(const Path<T2>& path) -> Path<T1> {
    const auto count = path.size();
    if (count == 0U) { return {}; }
    Path<T1> transformed(count);
    const auto* source = path.data();
    auto* target = transformed.data();
    if constexpr (std::is_same_v<T1, T2>) {
        for (std::size_t index = 0; index < count; ++index) { target[index] = source[index]; }
    } else {
        for (std::size_t index = 0; index < count; ++index) {
            target[index].x = geotypes::coordinateCast<T1>(
                source[index].x, geotypes::CoordinateRounding::NearestAwayFromZero);
            target[index].y = geotypes::coordinateCast<T1>(
                source[index].y, geotypes::CoordinateRounding::NearestAwayFromZero);
        }
    }
    return transformed;
}

template <>
[[nodiscard]] inline auto transform_path<int64_t, double>(const Path<double>& path)
    -> Path<int64_t> {
    const auto count = path.size();
    if (count == 0U) { return {}; }
    Path<int64_t> transformed(count);
    path_transform_detail::fill_floating_points_nonempty(
        path.data(), transformed.data(), count);
    return transformed;
}

#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
// A sized transformed range constructs each output value once. Toolchains
// without the C++23 container constructor use the existing generic transform.
template <>
[[nodiscard]] inline auto transform_path<double, int64_t>(const Path<int64_t>& path)
    -> Path<double> {
    auto coordinates = path | std::views::transform([](const Point64& point) {
        return PointD{static_cast<double>(point.x), static_cast<double>(point.y)};
    });
    return Path<double>(std::from_range, coordinates);
}
#endif

template <typename T1, typename T2>
[[nodiscard]] inline auto transform_paths(const Paths<T2>& paths) -> Paths<T1> {
    Paths<T1> transformed;
    transformed.reserve(paths.size());
    for (const auto& path : paths) { transformed.push_back(transform_path<T1, T2>(path)); }
    return transformed;
}

template <typename T>
[[nodiscard]] inline auto strip_near_equal(const Path<T>& path,
                                           double max_dist_sqrd,
                                           bool is_closed_path) -> Path<T> {
    Path<T> result;
    result.reserve(path.size());
    for (const auto& point : path) {
        if (result.empty() || !near_equal(point, result.back(), max_dist_sqrd)) {
            result.push_back(point);
        }
    }
    if (is_closed_path && !result.empty()) {
        const auto first = result.front();
        while (result.size() > 1 && near_equal(result.back(), first, max_dist_sqrd)) {
            result.pop_back();
        }
    }
    return result;
}

template <typename T>
[[nodiscard]] inline auto strip_near_equal(const Paths<T>& paths,
                                           double max_dist_sqrd,
                                           bool is_closed_path) -> Paths<T> {
    Paths<T> result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        result.push_back(strip_near_equal(path, max_dist_sqrd, is_closed_path));
    }
    return result;
}

}  // namespace clipper2next
