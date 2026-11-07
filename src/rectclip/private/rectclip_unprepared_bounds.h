#pragma once

#include "rectclip/private/rectclip_path_bounds.h"

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH 1
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET __attribute__((target("avx2")))
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_INLINE inline __attribute__((target("avx2"), always_inline))
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH 0
#elif defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#include <intrin.h>
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH 1
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET __declspec(noinline)
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_INLINE __forceinline
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH 1
#else
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH 0
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_INLINE inline
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH 0
#endif

#include <algorithm>
#include <cstdint>

namespace clipper2next::internal {
namespace {

template<bool CheckRange>
[[nodiscard]] auto path_set_bounds_scalar(
    const Paths64& paths, std::vector<Rect64>& path_bounds) -> bool {
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (paths[index].empty()) {
            path_bounds[index] = Rect64::invalid_rect();
            continue;
        }
        if (!rectclip_path_bounds_scalar(paths[index], CheckRange, path_bounds[index])) { return false; }
    }
    return true;
}

#if CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH
struct point_xy final {
    std::int64_t x{};
    std::int64_t y{};
};

[[nodiscard]] auto cpu_supports_avx2() noexcept -> bool {
    static const bool supported = [] {
#if CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH
        int registers[4]{};
        __cpuid(registers, 0);
        if (registers[0] < 7) { return false; }
        __cpuidex(registers, 1, 0);
        constexpr int osxsave_bit = 1 << 27;
        constexpr int avx_bit = 1 << 28;
        if ((registers[2] & (osxsave_bit | avx_bit)) != (osxsave_bit | avx_bit) ||
            (_xgetbv(0) & 0x6U) != 0x6U) {
            return false;
        }
        __cpuidex(registers, 7, 0);
        return (registers[1] & (1 << 5)) != 0;
#else
        __builtin_cpu_init();
        return __builtin_cpu_supports("avx2");
#endif
    }();
    return supported;
}

[[nodiscard]] CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_INLINE auto path_bounds_avx2(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
    if constexpr (sizeof(Point64) != sizeof(point_xy)) {
        return rectclip_path_bounds_scalar(path, check_coordinate_range, bounds);
    } else {
        // The eight-point reduction needs its initial pair plus eight points.
        if (path.size() < rectclip_vector_bounds_minimum_points) { return rectclip_path_bounds_scalar(path, check_coordinate_range, bounds); }
        auto minimums = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(path.data()));
        auto maximums = minimums;
        auto secondary_minimums = minimums;
        auto secondary_maximums = maximums;
        std::size_t index = 2U;
        for (; index + 7U < path.size(); index += 8U) {
            const auto first =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(path.data() + index));
            const auto second = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(path.data() + index + 2U));
            const auto first_greater = _mm256_cmpgt_epi64(first, second);
            const auto pair_minimum = _mm256_blendv_epi8(first, second, first_greater);
            const auto pair_maximum = _mm256_blendv_epi8(second, first, first_greater);
            minimums = _mm256_blendv_epi8(
                minimums, pair_minimum, _mm256_cmpgt_epi64(minimums, pair_minimum));
            maximums = _mm256_blendv_epi8(
                maximums, pair_maximum, _mm256_cmpgt_epi64(pair_maximum, maximums));

            const auto third = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(path.data() + index + 4U));
            const auto fourth = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(path.data() + index + 6U));
            const auto third_greater = _mm256_cmpgt_epi64(third, fourth);
            const auto secondary_minimum = _mm256_blendv_epi8(third, fourth, third_greater);
            const auto secondary_maximum = _mm256_blendv_epi8(fourth, third, third_greater);
            secondary_minimums = _mm256_blendv_epi8(
                secondary_minimums,
                secondary_minimum,
                _mm256_cmpgt_epi64(secondary_minimums, secondary_minimum));
            secondary_maximums = _mm256_blendv_epi8(
                secondary_maximums,
                secondary_maximum,
                _mm256_cmpgt_epi64(secondary_maximum, secondary_maximums));
        }
        for (; index + 3U < path.size(); index += 4U) {
            const auto first =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(path.data() + index));
            const auto second = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(path.data() + index + 2U));
            const auto first_greater = _mm256_cmpgt_epi64(first, second);
            const auto pair_minimum = _mm256_blendv_epi8(first, second, first_greater);
            const auto pair_maximum = _mm256_blendv_epi8(second, first, first_greater);
            minimums = _mm256_blendv_epi8(
                minimums, pair_minimum, _mm256_cmpgt_epi64(minimums, pair_minimum));
            maximums = _mm256_blendv_epi8(
                maximums, pair_maximum, _mm256_cmpgt_epi64(pair_maximum, maximums));
        }
        for (; index + 1U < path.size(); index += 2U) {
            const auto values =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(path.data() + index));
            minimums = _mm256_blendv_epi8(
                minimums, values, _mm256_cmpgt_epi64(minimums, values));
            maximums = _mm256_blendv_epi8(
                maximums, values, _mm256_cmpgt_epi64(values, maximums));
        }
        minimums = _mm256_blendv_epi8(
            minimums, secondary_minimums, _mm256_cmpgt_epi64(minimums, secondary_minimums));
        maximums = _mm256_blendv_epi8(
            maximums, secondary_maximums, _mm256_cmpgt_epi64(secondary_maximums, maximums));

        alignas(32) std::int64_t minimum[4];
        alignas(32) std::int64_t maximum[4];
        _mm256_store_si256(reinterpret_cast<__m256i*>(minimum), minimums);
        _mm256_store_si256(reinterpret_cast<__m256i*>(maximum), maximums);
        auto min_x = std::min(minimum[0], minimum[2]);
        auto min_y = std::min(minimum[1], minimum[3]);
        auto max_x = std::max(maximum[0], maximum[2]);
        auto max_y = std::max(maximum[1], maximum[3]);
        for (; index < path.size(); ++index) {
            min_x = std::min(min_x, path[index].x);
            min_y = std::min(min_y, path[index].y);
            max_x = std::max(max_x, path[index].x);
            max_y = std::max(max_y, path[index].y);
        }
        bounds = {min_x, min_y, max_x, max_y};
        return !check_coordinate_range ||
               (rectclip_coordinate_in_range(min_x) && rectclip_coordinate_in_range(min_y) &&
                rectclip_coordinate_in_range(max_x) && rectclip_coordinate_in_range(max_y));
    }
}

[[nodiscard]] CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET auto single_path_bounds_avx2(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
    return path_bounds_avx2(path, check_coordinate_range, bounds);
}

template<bool CheckRange>
[[nodiscard]] CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET auto path_set_bounds_avx2(
    const Paths64& paths, std::vector<Rect64>& path_bounds) -> bool {
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (paths[index].empty()) {
            path_bounds[index] = Rect64::invalid_rect();
            continue;
        }
        if (!path_bounds_avx2(paths[index], CheckRange, path_bounds[index])) { return false; }
    }
    return true;
}

#endif

}  // namespace

auto rectclip_large_path_bounds(const Path64& path, bool check_coordinate_range,
                          Rect64& bounds) -> bool {
#if CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH
    if (cpu_supports_avx2()) {
        return single_path_bounds_avx2(path, check_coordinate_range, bounds);
    }
#endif
    return rectclip_path_bounds_scalar(path, check_coordinate_range, bounds);
}

auto build_rectclip_path_bounds(const Paths64& paths, bool check_coordinate_range,
                               std::vector<Rect64>& path_bounds) -> bool {
    path_bounds.resize(paths.size());
    auto valid = false;
#if CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH
    if (cpu_supports_avx2()) {
        valid = check_coordinate_range ? path_set_bounds_avx2<true>(paths, path_bounds)
                                      : path_set_bounds_avx2<false>(paths, path_bounds);
    } else
#endif
    {
        valid = check_coordinate_range ? path_set_bounds_scalar<true>(paths, path_bounds)
                                      : path_set_bounds_scalar<false>(paths, path_bounds);
    }
    if (!valid) { path_bounds.clear(); }
    return valid;
}

}  // namespace clipper2next::internal

#undef CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH
#undef CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET
#undef CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_INLINE
#undef CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH
