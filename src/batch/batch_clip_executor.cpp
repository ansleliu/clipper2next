#include "batch/private/batch_clip_executor.h"

#include "clip/private/closed_clip_fast_path.h"
#include "support/private/checked_size.h"

#include <algorithm>
#include <numeric>

namespace clipper2next::internal {

namespace {

[[nodiscard]] auto paths_have_same_shape(const Paths64& left, const Paths64& right) -> bool {
    if (left.size() != right.size()) { return false; }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].size() != right[index].size()) { return false; }
    }
    return true;
}

[[nodiscard]] auto execution_options_have_same_work_shape(const execution_options& left,
                                                          const execution_options& right)
    -> bool {
    return left.preserve_collinear == right.preserve_collinear &&
           left.reverse_solution == right.reverse_solution &&
           left.intersection_policy.mode == right.intersection_policy.mode;
}

[[nodiscard]] auto clip_requests_have_same_shape(const clip_request64& left,
                                                 const clip_request64& right) -> bool {
    return left.clip_type == right.clip_type && left.fill_rule == right.fill_rule &&
           execution_options_have_same_work_shape(left.options, right.options) &&
           paths_have_same_shape(left.subjects, right.subjects) &&
           paths_have_same_shape(left.open_subjects, right.open_subjects) &&
           paths_have_same_shape(left.clips, right.clips);
}

}  // namespace

auto build_clip_batch_work_profile(std::size_t input_index,
                                   const clip_request64& request,
                                   const clip_request_metadata64& metadata)
    -> clip_batch_work_profile {
    const auto closed_point_count =
        checked_size_add(metadata.subject_point_count, metadata.clip_point_count);
    return clip_batch_work_profile{
        input_index,
        checked_size_add(closed_point_count, metadata.open_subject_point_count),
        is_likely_closed_clip_fast_path_candidate(request, metadata)};
}

auto clip_batch_requests_have_uniform_shape(std::span<const clip_request64> requests) -> bool {
    if (requests.size() < 2U) { return true; }

    const auto& first = requests.front();
    return std::all_of(requests.begin(), requests.end(), [&](const auto& request) {
        return clip_requests_have_same_shape(first, request);
    });
}

auto clip_batch_requests_are_parallel_safe(std::span<const clip_request64> requests) -> bool {
    static_cast<void>(requests);
    return true;
}

auto clip_batch_requests_are_parallel_safe(std::span<const prepared_clip_request64> requests)
    -> bool {
    static_cast<void>(requests);
    return true;
}

auto clip_batch_work_profiles_are_uniform(std::span<const clip_batch_work_profile> profiles)
    -> bool {
    if (profiles.size() < 2U) { return true; }

    const auto likely_fast_path = profiles.front().likely_fast_path;
    const auto estimated_point_count = profiles.front().estimated_point_count;
    return std::all_of(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return profile.likely_fast_path == likely_fast_path &&
               profile.estimated_point_count == estimated_point_count;
    });
}

auto build_clip_batch_work_order(std::span<const clip_batch_work_profile> profiles)
    -> std::vector<std::size_t> {
    std::vector<std::size_t> order(profiles.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const auto& left_profile = profiles[left];
        const auto& right_profile = profiles[right];
        if (left_profile.likely_fast_path != right_profile.likely_fast_path) {
            return !left_profile.likely_fast_path;
        }
        if (left_profile.estimated_point_count != right_profile.estimated_point_count) {
            return left_profile.estimated_point_count > right_profile.estimated_point_count;
        }
        return left_profile.input_index < right_profile.input_index;
    });
    for (auto& index : order) { index = profiles[index].input_index; }
    return order;
}

}  // namespace clipper2next::internal
