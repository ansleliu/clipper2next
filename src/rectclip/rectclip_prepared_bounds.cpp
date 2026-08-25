#include "rectclip/private/rectclip_facade_runner.h"

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#define CLIPPER2NEXT_RECTCLIP_GNU_AVX2_DISPATCH 1
#else
#define CLIPPER2NEXT_RECTCLIP_GNU_AVX2_DISPATCH 0
#endif

#include <cstdint>

namespace clipper2next::internal {
namespace {

struct point_xy final {
    std::int64_t x{};
    std::int64_t y{};
};

auto include_point(const Point64& point,
                   std::int64_t& min_x,
                   std::int64_t& max_x,
                   std::int64_t& min_y,
                   std::int64_t& max_y) -> void {
    if (point.x < min_x) { min_x = point.x; }
    if (point.x > max_x) { max_x = point.x; }
    if (point.y < min_y) { min_y = point.y; }
    if (point.y > max_y) { max_y = point.y; }
}

[[nodiscard]] auto path_bounds_scalar(const Path64& path, Rect64& bounds) -> bool {
    if (path.empty()) { return false; }
    auto min_x = path.front().x;
    auto max_x = path.front().x;
    auto min_y = path.front().y;
    auto max_y = path.front().y;
    std::size_t index = 1U;
    for (; index + 3U < path.size(); index += 4U) {
        include_point(path[index], min_x, max_x, min_y, max_y);
        include_point(path[index + 1U], min_x, max_x, min_y, max_y);
        include_point(path[index + 2U], min_x, max_x, min_y, max_y);
        include_point(path[index + 3U], min_x, max_x, min_y, max_y);
    }
    for (; index < path.size(); ++index) {
        include_point(path[index], min_x, max_x, min_y, max_y);
    }
    bounds = {min_x, min_y, max_x, max_y};
    return true;
}

#if CLIPPER2NEXT_RECTCLIP_GNU_AVX2_DISPATCH
[[nodiscard]] __attribute__((target("avx2"))) auto path_bounds_avx2(
    const Path64& path, Rect64& bounds) -> bool {
    if constexpr (sizeof(Point64) == sizeof(point_xy)) {
        if (path.size() >= 2U) {
            auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(path.data()));
            auto min_values = values;
            auto max_values = values;
            std::size_t index = 2U;
            for (; index + 1U < path.size(); index += 2U) {
                values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(path.data() + index));
                min_values = _mm256_blendv_epi8(
                    min_values, values, _mm256_cmpgt_epi64(min_values, values));
                max_values = _mm256_blendv_epi8(
                    max_values, values, _mm256_cmpgt_epi64(values, max_values));
            }
            alignas(32) std::int64_t minimum[4];
            alignas(32) std::int64_t maximum[4];
            _mm256_store_si256(reinterpret_cast<__m256i*>(minimum), min_values);
            _mm256_store_si256(reinterpret_cast<__m256i*>(maximum), max_values);
            auto min_x = minimum[0] < minimum[2] ? minimum[0] : minimum[2];
            auto min_y = minimum[1] < minimum[3] ? minimum[1] : minimum[3];
            auto max_x = maximum[0] > maximum[2] ? maximum[0] : maximum[2];
            auto max_y = maximum[1] > maximum[3] ? maximum[1] : maximum[3];
            for (; index < path.size(); ++index) {
                include_point(path[index], min_x, max_x, min_y, max_y);
            }
            bounds = {min_x, min_y, max_x, max_y};
            return true;
        }
    }
    return path_bounds_scalar(path, bounds);
}

[[nodiscard]] auto cpu_supports_avx2() -> bool {
    static const bool supported = [] {
        __builtin_cpu_init();
        return __builtin_cpu_supports("avx2");
    }();
    return supported;
}
#endif

[[nodiscard]] auto path_bounds_unchecked(const Path64& path, Rect64& bounds) -> bool {
#if CLIPPER2NEXT_RECTCLIP_GNU_AVX2_DISPATCH
    if (cpu_supports_avx2()) { return path_bounds_avx2(path, bounds); }
#endif
    return path_bounds_scalar(path, bounds);
}

auto include_bounds(Rect64& combined, const Rect64& bounds) -> void {
    if (bounds.left < combined.left) { combined.left = bounds.left; }
    if (bounds.top < combined.top) { combined.top = bounds.top; }
    if (bounds.right > combined.right) { combined.right = bounds.right; }
    if (bounds.bottom > combined.bottom) { combined.bottom = bounds.bottom; }
}

}  // namespace

auto rectclip_paths_have_minimum_size(const Paths64& paths, std::size_t minimum_size) -> bool {
    for (const auto& path : paths) {
        if (path.size() < minimum_size) { return false; }
    }
    return true;
}

auto rectclip_has_precomputed_bounds(
    const Paths64& paths, std::span<const Rect64> path_bounds) -> bool {
    return !paths.empty() && path_bounds.size() == paths.size();
}

auto rectclip_paths_bounds_unchecked(const Paths64& paths, Rect64& combined_bounds) -> bool {
    bool has_bounds = false;
    for (const auto& path : paths) {
        Rect64 current_bounds;
        if (!path_bounds_unchecked(path, current_bounds)) { continue; }
        if (!has_bounds) {
            combined_bounds = current_bounds;
            has_bounds = true;
        } else {
            include_bounds(combined_bounds, current_bounds);
        }
    }
    return has_bounds;
}

auto rectclip_rect_contains_bounds(
    const Rect64& rect, const Rect64& path_bounds, rectclip_mode mode) -> bool {
    if (mode == rectclip_mode::polygons) { return rect.contains(path_bounds); }
    return path_bounds.left > rect.left && path_bounds.top > rect.top &&
           path_bounds.right < rect.right && path_bounds.bottom < rect.bottom;
}

}  // namespace clipper2next::internal
