#pragma once

#include "clipper2next/clip/request.h"

#include <cstddef>
#include <span>
#include <vector>

namespace clipper2next::internal {

struct clip_batch_work_profile final {
    std::size_t input_index{};
    std::size_t estimated_point_count{};
    bool likely_fast_path{};
};

[[nodiscard]] constexpr auto clip_batch_parallel_threshold() noexcept -> std::size_t {
    return 1024U;
}

[[nodiscard]] constexpr auto clip_batch_parallel_grain_size() noexcept -> std::size_t {
    return 8U;
}

[[nodiscard]] constexpr auto clip_batch_parallel_maximum_concurrency()
    noexcept -> std::size_t {
    return 32U;
}

[[nodiscard]] auto build_clip_batch_work_profile(std::size_t input_index,
                                                 const clip_request64& request,
                                                 const clip_request_metadata64& metadata)
    -> clip_batch_work_profile;
[[nodiscard]] auto clip_batch_requests_have_uniform_shape(
    std::span<const clip_request64> requests) -> bool;
[[nodiscard]] auto clip_batch_requests_are_parallel_safe(
    std::span<const clip_request64> requests) -> bool;
[[nodiscard]] auto clip_batch_requests_are_parallel_safe(
    std::span<const prepared_clip_request64> requests) -> bool;
[[nodiscard]] auto clip_batch_work_profiles_are_uniform(
    std::span<const clip_batch_work_profile> profiles) -> bool;
[[nodiscard]] auto build_clip_batch_work_order(std::span<const clip_batch_work_profile> profiles)
    -> std::vector<std::size_t>;

}  // namespace clipper2next::internal
