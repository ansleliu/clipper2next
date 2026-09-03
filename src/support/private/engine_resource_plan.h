#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/offset/types.h"

#include <cstddef>

namespace clipper2next::internal {

struct engine_resource_plan final {
    std::size_t work{};
    std::size_t workspace_bytes{};
};

struct offset_engine_resource_context final {
    engine_resource_plan generation{};
    engine_resource_plan selected{};
    std::size_t maximum_work{};
    std::size_t maximum_workspace_bytes{};
};

[[nodiscard]] auto plan_clip_engine_resources(std::size_t input_point_count)
    noexcept -> engine_resource_plan;

[[nodiscard]] auto plan_offset_generation_resources(
    std::size_t input_path_count,
    std::size_t input_point_count,
    std::size_t maximum_path_point_count,
    double delta,
    JoinType join_type,
    EndType end_type,
    double arc_tolerance,
    std::size_t arc_segments_per_quadrant,
    std::size_t concurrency_limit) noexcept -> engine_resource_plan;

[[nodiscard]] auto finalize_offset_engine_resources(
    offset_engine_resource_context& context,
    std::size_t output_path_count,
    std::size_t output_point_count,
    bool requires_cleanup) noexcept -> clipper_error_code;

[[nodiscard]] auto measure_offset_path_storage(
    std::size_t path_count,
    std::size_t point_count,
    std::size_t path_record_size,
    std::size_t& result) noexcept -> clipper_error_code;

[[nodiscard]] auto add_workspace_bytes(
    std::size_t first,
    std::size_t second,
    std::size_t& result) noexcept -> clipper_error_code;

}  // namespace clipper2next::internal
