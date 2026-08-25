#include "rectclip/private/rectclip_path_bounds.h"

#if defined(__AVX2__) || defined(_M_AVX2)
#include <immintrin.h>
#define CLIPPER2NEXT_RECTCLIP_HAS_AVX2 1
#else
#define CLIPPER2NEXT_RECTCLIP_HAS_AVX2 0
#endif

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto coordinate_in_range(std::int64_t value) -> bool {
    return value >= MIN_COORD && value <= MAX_COORD;
}

struct point_xy final {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

auto include_bounds(rectclip_path_bounds_summary& summary, const Rect64& bounds) -> void {
    if (!summary.has_bounds) {
        summary.combined_bounds = bounds;
        summary.has_bounds = true;
        return;
    }
    if (bounds.left < summary.combined_bounds.left) {
        summary.combined_bounds.left = bounds.left;
    }
    if (bounds.top < summary.combined_bounds.top) { summary.combined_bounds.top = bounds.top; }
    if (bounds.right > summary.combined_bounds.right) {
        summary.combined_bounds.right = bounds.right;
    }
    if (bounds.bottom > summary.combined_bounds.bottom) {
        summary.combined_bounds.bottom = bounds.bottom;
    }
}

[[nodiscard]] auto path_bounds_if_in_range(const Path64& path, Rect64& current_bounds) -> bool {
    if (path.empty()) { return false; }
#if CLIPPER2NEXT_RECTCLIP_HAS_AVX2
    if constexpr (sizeof(Point64) == sizeof(point_xy)) {
        if (path.size() >= 2U) {
            const auto lower = _mm256_set1_epi64x(MIN_COORD);
            const auto upper = _mm256_set1_epi64x(MAX_COORD);
            auto values = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(path.data()));
            auto below = _mm256_cmpgt_epi64(lower, values);
            auto above = _mm256_cmpgt_epi64(values, upper);
            auto outside = _mm256_or_si256(below, above);
            if (_mm256_movemask_pd(_mm256_castsi256_pd(outside)) != 0) { return false; }

            auto min_values = values;
            auto max_values = values;
            std::size_t point_index = 2U;
            for (; point_index + 1U < path.size(); point_index += 2U) {
                values = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(path.data() + point_index));
                below = _mm256_cmpgt_epi64(lower, values);
                above = _mm256_cmpgt_epi64(values, upper);
                outside = _mm256_or_si256(below, above);
                if (_mm256_movemask_pd(_mm256_castsi256_pd(outside)) != 0) {
                    return false;
                }

                auto select_min = _mm256_cmpgt_epi64(min_values, values);
                min_values = _mm256_blendv_epi8(min_values, values, select_min);
                auto select_max = _mm256_cmpgt_epi64(values, max_values);
                max_values = _mm256_blendv_epi8(max_values, values, select_max);
            }

            alignas(32) std::int64_t min_lanes[4];
            alignas(32) std::int64_t max_lanes[4];
            _mm256_store_si256(reinterpret_cast<__m256i*>(min_lanes), min_values);
            _mm256_store_si256(reinterpret_cast<__m256i*>(max_lanes), max_values);
            auto min_x = min_lanes[0] < min_lanes[2] ? min_lanes[0] : min_lanes[2];
            auto min_y = min_lanes[1] < min_lanes[3] ? min_lanes[1] : min_lanes[3];
            auto max_x = max_lanes[0] > max_lanes[2] ? max_lanes[0] : max_lanes[2];
            auto max_y = max_lanes[1] > max_lanes[3] ? max_lanes[1] : max_lanes[3];

            for (; point_index < path.size(); ++point_index) {
                const auto& point = path[point_index];
                if (!coordinate_in_range(point.x) || !coordinate_in_range(point.y)) {
                    return false;
                }
                if (point.x < min_x) {
                    min_x = point.x;
                } else if (point.x > max_x) {
                    max_x = point.x;
                }
                if (point.y < min_y) {
                    min_y = point.y;
                } else if (point.y > max_y) {
                    max_y = point.y;
                }
            }
            current_bounds = Rect64{min_x, min_y, max_x, max_y};
            return true;
        }
    }
#endif
    auto min_x = path.front().x;
    auto max_x = path.front().x;
    auto min_y = path.front().y;
    auto max_y = path.front().y;
    for (const auto& point : path) {
        if (!coordinate_in_range(point.x) || !coordinate_in_range(point.y)) { return false; }
        if (point.x < min_x) {
            min_x = point.x;
        } else if (point.x > max_x) {
            max_x = point.x;
        }
        if (point.y < min_y) {
            min_y = point.y;
        } else if (point.y > max_y) {
            max_y = point.y;
        }
    }
    current_bounds = Rect64{min_x, min_y, max_x, max_y};
    return true;
}

}  // namespace

auto rectclip_rect_in_range(const Rect64& rect) -> bool {
    return coordinate_in_range(rect.left) && coordinate_in_range(rect.top) &&
           coordinate_in_range(rect.right) && coordinate_in_range(rect.bottom);
}

auto build_rectclip_path_bounds_if_in_range(const Paths64& paths,
                                            std::vector<Rect64>& path_bounds) -> bool {
    rectclip_path_bounds_summary summary;
    return build_rectclip_path_bounds_if_in_range(paths, path_bounds, summary);
}

auto build_rectclip_path_bounds_if_in_range(const Paths64& paths,
                                            std::vector<Rect64>& path_bounds,
                                            rectclip_path_bounds_summary& summary) -> bool {
    summary = {};
    path_bounds.clear();
    path_bounds.reserve(paths.size());
    for (const auto& path : paths) {
        if (path.size() < 3U) { summary.all_paths_have_polygon_minimum_size = false; }
        if (path.size() < 2U) { summary.all_paths_have_line_minimum_size = false; }
        if (path.empty()) {
            path_bounds.push_back(Rect64::invalid_rect());
            continue;
        }

        Rect64 current_bounds;
        if (!path_bounds_if_in_range(path, current_bounds)) {
            path_bounds.clear();
            return false;
        }
        include_bounds(summary, current_bounds);
        path_bounds.emplace_back(current_bounds);
    }
    return true;
}

auto summarize_rectclip_path_bounds(const Paths64& paths, std::span<const Rect64> path_bounds)
    -> rectclip_path_bounds_summary {
    rectclip_path_bounds_summary summary;
    if (path_bounds.size() != paths.size()) { return summary; }
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto& path = paths[index];
        if (path.size() < 3U) { summary.all_paths_have_polygon_minimum_size = false; }
        if (path.size() < 2U) { summary.all_paths_have_line_minimum_size = false; }
        if (path.empty()) { continue; }
        include_bounds(summary, path_bounds[index]);
    }
    return summary;
}

}  // namespace clipper2next::internal
