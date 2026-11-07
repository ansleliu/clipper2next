#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/core/path.h"

#include <algorithm>
#include <bit>
#include <ranges>
#include <type_traits>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace clipper2next {

namespace path_translation_detail {
#if defined(__x86_64__) || defined(_M_X64)
[[nodiscard]] inline auto sse42_available() noexcept -> bool {
    static const bool available = [] {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<bool>(__builtin_cpu_supports("sse4.2")) &&
               static_cast<bool>(__builtin_cpu_supports("sse4.1"));
#elif defined(_MSC_VER)
        int registers[4];
        __cpuid(registers, 1);
        constexpr auto required = (1 << 19) | (1 << 20);
        return (registers[2] & required) == required;
#else
        return false;
#endif
    }();
    return available;
}

class int64_translation_sse42 final {
public:
    int64_translation_sse42(std::int64_t dx, std::int64_t dy) noexcept {
        static_assert(sizeof(Point64) == 16U && std::is_trivially_copyable_v<Point64>);
        static_assert(offsetof(Point64, x) == 0 && offsetof(Point64, y) == sizeof(std::int64_t));
        constexpr auto maximum = (std::numeric_limits<std::int64_t>::max)();
        constexpr auto minimum = (std::numeric_limits<std::int64_t>::lowest)();
        delta_ = _mm_set_epi64x(dy, dx);
        negative_ = _mm_set_epi64x(dy < 0 ? -1 : 0, dx < 0 ? -1 : 0);
        limits_ = _mm_set_epi64x(dy < 0 ? maximum + dy : maximum - dy,
                                dx < 0 ? maximum + dx : maximum - dx);
        clamps_ = _mm_set_epi64x(dy < 0 ? minimum : maximum, dx < 0 ? minimum : maximum);
    }

    [[nodiscard]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((target("sse4.2")))
#endif
    auto operator()(const Point64& point) const noexcept -> Point64 {
        const auto points = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&point));
        const auto overflow = _mm_cmpgt_epi64(_mm_xor_si128(points, negative_), limits_);
        const auto result = _mm_blendv_epi8(_mm_add_epi64(points, delta_), clamps_, overflow);
        return std::bit_cast<Point64>(result);
    }

private:
    __m128i delta_;
    __m128i negative_;
    __m128i limits_;
    __m128i clamps_;
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse4.2"), noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
inline auto fill_int64_sse42(const Point64* source, Point64* target,
                            std::size_t count, std::int64_t dx, std::int64_t dy) -> void {
    const auto shifted = int64_translation_sse42{dx, dy};
    for (std::size_t index = 0; index < count; ++index) {
        target[index] = shifted(source[index]);
    }
}
#endif
}  // namespace path_translation_detail


template <typename T>
[[nodiscard]] inline auto translate(const Path<T>& path, T dx, T dy) -> Path<T> {
    if (path.empty()) { return {}; }
    const auto count = path.size();
#if defined(__x86_64__) || defined(_M_X64)
    if constexpr (std::is_same_v<T, std::int64_t>) {
        if (path_translation_detail::sse42_available()) {
#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
            auto coordinates = path | std::views::transform(
                path_translation_detail::int64_translation_sse42{dx, dy});
            return Path<T>(std::from_range, coordinates);
#else
            Path<T> translated(count);
            path_translation_detail::fill_int64_sse42(
                path.data(), translated.data(), count, dx, dy);
            return translated;
#endif
        }
    }
#endif
    Path<T> translated(count);
    const auto* source = path.data();
    auto* target = translated.data();
    for (std::size_t index = 0; index < count; ++index) {
        target[index].x = geotypes::saturatedAdd(source[index].x, dx);
        target[index].y = geotypes::saturatedAdd(source[index].y, dy);
    }
    return translated;
}

[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const Path64& path, int64_t dx, int64_t dy) -> Path64;
[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const PathD& path, double dx, double dy) -> PathD;

template <typename T>
[[nodiscard]] inline auto translate(const Paths<T>& paths, T dx, T dy) -> Paths<T> {
    auto translated = Paths<T>{};
    translated.reserve(paths.size());
    for (const auto& path : paths) { translated.push_back(translate(path, dx, dy)); }
    return translated;
}

[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const Paths64& paths, int64_t dx, int64_t dy) -> Paths64;
[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const PathsD& paths, double dx, double dy) -> PathsD;

}  // namespace clipper2next
