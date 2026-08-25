#pragma once

#include "rectclip/private/rectclip_unprepared_runner.h"

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH 1
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET __attribute__((target("avx2")))
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH 0
#elif defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#include <intrin.h>
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH 1
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET __declspec(noinline)
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH 1
#else
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH 0
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET
#define CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH 0
#endif

#include <algorithm>
#include <cstdint>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto coordinate_in_range(std::int64_t value) -> bool {
    constexpr auto span =
        static_cast<std::uint64_t>(MAX_COORD) - static_cast<std::uint64_t>(MIN_COORD);
    return static_cast<std::uint64_t>(value) - static_cast<std::uint64_t>(MIN_COORD) <= span;
}

[[nodiscard]] auto path_bounds_scalar(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
    if (path.empty()) { return false; }
    auto min_x = path.front().x;
    auto max_x = path.front().x;
    auto min_y = path.front().y;
    auto max_y = path.front().y;
    for (auto point = path.begin() + 1; point != path.end(); ++point) {
        min_x = std::min(min_x, point->x);
        max_x = std::max(max_x, point->x);
        min_y = std::min(min_y, point->y);
        max_y = std::max(max_y, point->y);
    }
    bounds = {min_x, min_y, max_x, max_y};
    return !check_coordinate_range ||
           (coordinate_in_range(min_x) && coordinate_in_range(min_y) &&
            coordinate_in_range(max_x) && coordinate_in_range(max_y));
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

[[nodiscard]] CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET auto path_bounds_avx2(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
    if constexpr (sizeof(Point64) != sizeof(point_xy)) {
        return path_bounds_scalar(path, check_coordinate_range, bounds);
    } else {
        if (path.size() < 2U) { return path_bounds_scalar(path, check_coordinate_range, bounds); }
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
               (coordinate_in_range(min_x) && coordinate_in_range(min_y) &&
                coordinate_in_range(max_x) && coordinate_in_range(max_y));
    }
}
#endif

}  // namespace

auto rectclip_unprepared_avx2_supported() noexcept -> bool {
#if CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH
    return cpu_supports_avx2();
#else
    return false;
#endif
}

auto rectclip_unprepared_path_bounds_scalar(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
    return path_bounds_scalar(path, check_coordinate_range, bounds);
}

auto rectclip_unprepared_path_bounds_avx2(
    const Path64& path, bool check_coordinate_range, Rect64& bounds) -> bool {
#if CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH
    return path_bounds_avx2(path, check_coordinate_range, bounds);
#else
    return path_bounds_scalar(path, check_coordinate_range, bounds);
#endif
}

}  // namespace clipper2next::internal

#undef CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_DISPATCH
#undef CLIPPER2NEXT_RECTCLIP_UNPREPARED_AVX2_TARGET
#undef CLIPPER2NEXT_RECTCLIP_UNPREPARED_MSVC_AVX2_DISPATCH
