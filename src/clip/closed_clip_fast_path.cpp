#include "clip/private/closed_clip_fast_path.h"

#include "clipper2next/core/rect.h"
#include "clip/private/clip_request_metadata.h"

#include <algorithm>
#include <utility>

namespace clipper2next::internal {

[[nodiscard]] auto contained_intersection_fast_path_point_threshold() noexcept -> std::size_t;
[[nodiscard]] auto try_execute_containing_rectangle_intersection(
    const clip_request64& request, const clip_request_metadata64& metadata, paths64_result& result)
    -> bool;
[[nodiscard]] auto try_execute_disjoint_bounds_intersection(
    const clip_request64& request, const clip_request_metadata64& metadata, paths64_result& result)
    -> bool;
[[nodiscard]] auto try_execute_two_rectangle_intersection(const clip_request64& request,
                                                          const clip_request_metadata64& metadata,
                                                          paths64_result& result) -> bool;
[[nodiscard]] auto try_execute_two_rectangle_union(const clip_request64& request,
                                                   const clip_request_metadata64& metadata,
                                                   paths64_result& result) -> bool;
[[nodiscard]] auto try_execute_two_rectangle_difference(const clip_request64& request,
                                                        const clip_request_metadata64& metadata,
                                                        paths64_result& result) -> bool;
[[nodiscard]] auto try_execute_two_rectangle_xor(const clip_request64& request,
                                                 const clip_request_metadata64& metadata,
                                                 paths64_result& result) -> bool;

namespace {

[[nodiscard]] auto fill_rule_is_orientation_insensitive(FillRule fill_rule) noexcept -> bool {
    // Positive/Negative depend on input winding direction, which the rectangle
    // and containment fast paths deliberately ignore (a clockwise rectangle is
    // empty under Positive fill). Restrict fast paths to the two fill rules
    // whose result is independent of winding direction for simple paths.
    return fill_rule == FillRule::EvenOdd || fill_rule == FillRule::NonZero;
}

[[nodiscard]] auto closed_clip_fast_path_options_are_eligible(
    const clip_request64& request) noexcept -> bool {
    return request.open_subjects.empty() &&
           request.options.preserve_collinear && !request.options.reverse_solution &&
           request.clip_type != ClipType::NoClip &&
           fill_rule_is_orientation_insensitive(request.fill_rule);
}

[[nodiscard]] auto try_execute_closed_clip_fast_path_impl(const clip_request64& request,
                                                          const clip_request_metadata64& metadata,
                                                          paths64_result& result) -> bool {
    if (metadata.open_subject_path_count != 0U) { return false; }
    if (!closed_clip_fast_path_options_are_eligible(request)) { return false; }

    if (try_execute_containing_rectangle_intersection(request, metadata, result)) { return true; }
    if (try_execute_two_rectangle_intersection(request, metadata, result)) { return true; }
    if (try_execute_two_rectangle_union(request, metadata, result)) { return true; }
    if (try_execute_two_rectangle_xor(request, metadata, result)) { return true; }
    if (try_execute_two_rectangle_difference(request, metadata, result)) { return true; }
    return try_execute_disjoint_bounds_intersection(request, metadata, result);
}

}  // namespace

auto is_likely_closed_clip_fast_path_candidate(const clip_request64& request,
                                               const clip_request_metadata64& metadata) -> bool {
    if (metadata.open_subject_path_count != 0U) { return false; }
    if (!request.options.preserve_collinear || request.options.reverse_solution) { return false; }
    if (!fill_rule_is_orientation_insensitive(request.fill_rule)) { return false; }
    if (request.clip_type == ClipType::Intersection) {
        const auto point_threshold = contained_intersection_fast_path_point_threshold();
        const auto point_count_within_threshold =
            metadata.subject_point_count <= point_threshold &&
            metadata.clip_point_count <= point_threshold - metadata.subject_point_count;
        return metadata.clip_path_count == 1U && metadata.subject_path_count != 0U &&
               metadata.single_clip_rect.has_value() &&
               point_count_within_threshold;
    }
    if (request.clip_type == ClipType::Union) {
        return metadata.subject_path_count == 1U && metadata.clip_path_count == 1U &&
               metadata.single_subject_rect.has_value() && metadata.single_clip_rect.has_value();
    }
    if (request.clip_type == ClipType::Difference) {
        return metadata.subject_path_count == 1U && metadata.clip_path_count == 1U &&
               metadata.single_subject_rect.has_value() && metadata.single_clip_rect.has_value();
    }
    if (request.clip_type == ClipType::Xor) {
        return metadata.subject_path_count == 1U && metadata.clip_path_count == 1U &&
               metadata.single_subject_rect.has_value() && metadata.single_clip_rect.has_value();
    }
    return false;
}

auto try_execute_closed_clip_fast_path(const clip_request64& request, paths64_result& result)
    -> bool {
    if (!closed_clip_fast_path_options_are_eligible(request)) { return false; }
    const auto metadata = build_clip_request_metadata(request);
    return try_execute_closed_clip_fast_path_impl(request, metadata, result);
}

auto try_execute_closed_clip_fast_path(const clip_request64& request,
                                       const clip_request_metadata64& metadata,
                                       paths64_result& result) -> bool {
    return try_execute_closed_clip_fast_path_impl(request, metadata, result);
}

auto try_execute_closed_clip_fast_path(const prepared_clip_request64& request,
                                       paths64_result& result) -> bool {
    return try_execute_closed_clip_fast_path_impl(request.request(), request.metadata(), result);
}

}  // namespace clipper2next::internal
