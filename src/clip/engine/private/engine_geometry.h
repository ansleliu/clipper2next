#pragma once

#include "clip/engine/private/engine_types.h"
#include "clipper2next/geometry/math.h"

#include <cmath>
#include <limits>

#if defined(__AVX2__) || defined(_M_AVX2)
#include <smmintrin.h>
#elif defined(_MSC_VER) && (defined(_M_AMD64) || defined(_M_X64))
#include <emmintrin.h>
#endif

namespace clipper2next::internal {

#if defined(_MSC_VER)
#define CLIPPER2NEXT_ENGINE_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define CLIPPER2NEXT_ENGINE_FORCE_INLINE inline __attribute__((always_inline))
#else
#define CLIPPER2NEXT_ENGINE_FORCE_INLINE inline
#endif

[[nodiscard]] CLIPPER2NEXT_ENGINE_FORCE_INLINE auto round_to_even_int64_in_clipper_range(
    double value) noexcept -> int64_t {
#if defined(__AVX2__) || defined(_M_AVX2)
    const auto rounded = _mm_round_sd(
        _mm_setzero_pd(), _mm_set_sd(value), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    return _mm_cvttsd_si64(rounded);
#elif defined(_MSC_VER) && (defined(_M_AMD64) || defined(_M_X64))
    return _mm_cvtsd_si64(_mm_set_sd(value));
#elif defined(__clang__)
#if __has_builtin(__builtin_elementwise_roundeven)
    return static_cast<int64_t>(__builtin_elementwise_roundeven(value));
#elif __has_builtin(__builtin_roundeven)
    return static_cast<int64_t>(__builtin_roundeven(value));
#else
    auto integer = static_cast<int64_t>(value);
    const auto fraction = value - static_cast<double>(integer);
    const auto is_odd = (integer & int64_t{1}) != 0;
    if (fraction > 0.5 || (fraction == 0.5 && is_odd)) {
        ++integer;
    } else if (fraction < -0.5 || (fraction == -0.5 && is_odd)) {
        --integer;
    }
    return integer;
#endif
#elif defined(__GNUC__)
    return static_cast<int64_t>(__builtin_roundeven(value));
#else
    auto integer = static_cast<int64_t>(value);
    const auto fraction = value - static_cast<double>(integer);
    const auto is_odd = (integer & int64_t{1}) != 0;
    if (fraction > 0.5 || (fraction == 0.5 && is_odd)) {
        ++integer;
    } else if (fraction < -0.5 || (fraction == -0.5 && is_odd)) {
        --integer;
    }
    return integer;
#endif
}

[[nodiscard]] inline auto get_dx(const Point64& first, const Point64& second) -> double {
    const auto dy = static_cast<double>(second.y - first.y);
    if (dy != 0.0) { return static_cast<double>(second.x - first.x) / dy; }
    if (second.x > first.x) { return -std::numeric_limits<double>::max(); }
    return std::numeric_limits<double>::max();
}

[[nodiscard]] CLIPPER2NEXT_ENGINE_FORCE_INLINE auto top_x(
    const active_edge_node& edge, const int64_t current_y) -> int64_t {
    if ((current_y == edge.top_point.y) || (edge.top_point.x == edge.bottom.x)) {
        return edge.top_point.x;
    }
    if (current_y == edge.bottom.y) { return edge.bottom.x; }
    return edge.bottom.x +
           round_to_even_int64_in_clipper_range(
               edge.dx * static_cast<double>(current_y - edge.bottom.y));
}

[[nodiscard]] inline auto is_horizontal(const active_edge_node& edge) -> bool {
    return edge.top_point.y == edge.bottom.y;
}

[[nodiscard]] inline auto is_heading_right_horizontal(const active_edge_node& edge) -> bool {
    return edge.dx == -std::numeric_limits<double>::max();
}

[[nodiscard]] inline auto is_heading_left_horizontal(const active_edge_node& edge) -> bool {
    return edge.dx == std::numeric_limits<double>::max();
}

#undef CLIPPER2NEXT_ENGINE_FORCE_INLINE

}  // namespace clipper2next::internal
