#include "minkowski/private/minkowski.h"

#include "clip/private/boolean_union_service.h"
#include "support/private/checked_size.h"

#include <utility>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto translated_minkowski_point(const Point64& path_point,
                                               const Point64& pattern_point,
                                               bool is_sum) -> Point64 {
    return {
        is_sum ? path_point.x + pattern_point.x : path_point.x - pattern_point.x,
        is_sum ? path_point.y + pattern_point.y : path_point.y - pattern_point.y,
    };
}

auto translate_minkowski_pattern(const Point64& path_point,
                                 const Path64& pattern,
                                 bool is_sum,
                                 Path64& translated_pattern) -> void {
    translated_pattern.resize(pattern.size());
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        translated_pattern[index] = translated_minkowski_point(path_point, pattern[index], is_sum);
    }
}

}  // namespace

auto build_minkowski_quads(const Path64& pattern,
                           const Path64& path,
                           bool is_sum,
                           bool is_closed) -> Paths64 {
    const size_t delta = is_closed ? 0 : 1;
    const size_t pattern_length = pattern.size();
    const size_t path_length = path.size();
    if (pattern_length == 0 || path_length == 0) { return {}; }

    Paths64 result;
    const auto segment_count = path_length - delta;
    const auto quad_count = checked_size_multiply(segment_count, pattern_length);
    if (quad_count > result.max_size()) { return result; }
    result.reserve(quad_count);
    Path64 previous_translated_pattern;
    Path64 current_translated_pattern;
    const size_t previous_path_index = is_closed ? path_length - 1 : 0;
    translate_minkowski_pattern(
        path[previous_path_index], pattern, is_sum, previous_translated_pattern);
    for (size_t path_index = delta; path_index < path_length; ++path_index) {
        translate_minkowski_pattern(
            path[path_index], pattern, is_sum, current_translated_pattern);
        size_t previous_pattern_index = pattern_length - 1;
        for (size_t pattern_index = 0; pattern_index < pattern_length; ++pattern_index) {
            const auto& p0 = previous_translated_pattern[previous_pattern_index];
            const auto& p1 = current_translated_pattern[previous_pattern_index];
            const auto& p2 = current_translated_pattern[pattern_index];
            const auto& p3 = previous_translated_pattern[pattern_index];
            const auto twice_area =
                static_cast<double>(p3.y + p0.y) * static_cast<double>(p3.x - p0.x) +
                static_cast<double>(p0.y + p1.y) * static_cast<double>(p0.x - p1.x) +
                static_cast<double>(p1.y + p2.y) * static_cast<double>(p1.x - p2.x) +
                static_cast<double>(p2.y + p3.y) * static_cast<double>(p2.x - p3.x);

            auto& quad = result.emplace_back();
            quad.resize(4);
            if (twice_area >= 0.0) {
                quad[0] = p0;
                quad[1] = p1;
                quad[2] = p2;
                quad[3] = p3;
            } else {
                quad[0] = p3;
                quad[1] = p2;
                quad[2] = p1;
                quad[3] = p0;
            }
            previous_pattern_index = pattern_index;
        }
        std::swap(previous_translated_pattern, current_translated_pattern);
    }
    return result;
}

auto union_minkowski_quads(Paths64&& subjects, FillRule fill_rule) -> Paths64 {
    clip_union_options options{fill_rule, execution_options{}};
    options.options.preserve_collinear = true;
    options.decompose_disjoint_components = false;
    return union_closed_paths(std::move(subjects), options);
}

}  // namespace clipper2next::internal
