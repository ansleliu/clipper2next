#include "api/private/engine_resource_plan.h"

#include "clip/engine/private/engine_types.h"
#include "offset/private/offset_geometry.h"
#include "offset/private/offset_group_processor.h"
#include "offset/private/offset_state.h"

#include <limits>

namespace clipper2next::internal {
namespace {

[[nodiscard]] constexpr auto saturating_add(
    const std::size_t first,
    const std::size_t second) noexcept -> std::size_t {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    return second > maximum - first ? maximum : first + second;
}

[[nodiscard]] constexpr auto saturating_product(
    const std::size_t first,
    const std::size_t second) noexcept -> std::size_t {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    return first != 0U && second > maximum / first
        ? maximum
        : first * second;
}

[[nodiscard]] auto plan_for_point_bound(
    const std::size_t point_bound) noexcept -> engine_resource_plan {
    const auto point_pairs = saturating_product(point_bound, point_bound);
    const auto work = saturating_add(
        saturating_product(point_pairs, 4U),
        saturating_product(point_bound, 8U));
    const auto per_point_bytes =
        sizeof(Point64) + sizeof(PointD) + sizeof(active_edge_node);
    const auto per_pair_bytes =
        sizeof(IntersectNode) + sizeof(output_point_node) +
        sizeof(output_record_node);
    return engine_resource_plan{
        .work = work,
        .workspace_bytes = saturating_add(
            saturating_product(point_bound, per_point_bytes),
            saturating_product(point_pairs, per_pair_bytes)),
    };
}

}  // namespace

auto plan_clip_engine_resources(const std::size_t input_point_count) noexcept
    -> engine_resource_plan {
    return plan_for_point_bound(input_point_count);
}

auto plan_offset_engine_resources(
    const std::size_t input_path_count,
    const std::size_t input_point_count,
    const std::size_t maximum_path_point_count,
    const double delta,
    const JoinType join_type,
    const EndType end_type,
    const double arc_tolerance,
    const std::size_t arc_segments_per_quadrant,
    const std::size_t concurrency_limit) noexcept
    -> engine_resource_plan {
    auto expansion = std::size_t{3U};
    if (join_type == JoinType::Round || end_type == EndType::Round) {
        const auto arc = make_arc_parameters(
            delta, arc_tolerance, arc_segments_per_quadrant);
        expansion = saturating_add(arc_step_count(arc, pi), 2U);
    }
    const auto point_bound =
        saturating_product(input_point_count, expansion);
    auto plan = plan_for_point_bound(point_bound);
    if (concurrency_limit < offset_parallel_minimum_concurrency ||
        input_path_count < offset_parallel_minimum_path_count ||
        input_point_count < offset_parallel_minimum_point_count) {
        return plan;
    }

    const auto active_concurrency =
        std::min(concurrency_limit, input_path_count);
    const auto chunk_count = std::min(
        input_path_count, saturating_product(active_concurrency, 4U));
    const auto planning_bytes = saturating_product(
        chunk_count,
        2U * sizeof(std::size_t) + sizeof(path_set64));
    const auto retained_output_bytes = saturating_product(
        point_bound, sizeof(Point64));
    const auto per_task_scratch_bytes = saturating_add(
        sizeof(offset_state),
        saturating_add(
            saturating_product(
                maximum_path_point_count, sizeof(PointD)),
            saturating_product(
                saturating_product(maximum_path_point_count, expansion),
                sizeof(Point64))));
    const auto parallel_phase_bytes = saturating_add(
        planning_bytes,
        saturating_add(
            retained_output_bytes,
            saturating_product(
                active_concurrency, per_task_scratch_bytes)));
    const auto merge_phase_bytes = saturating_add(
        planning_bytes,
        saturating_product(retained_output_bytes, 2U));
    plan.workspace_bytes = saturating_add(
        plan.workspace_bytes,
        std::max(parallel_phase_bytes, merge_phase_bytes));
    return plan;
}

auto measure_offset_path_storage(
    const std::size_t path_count,
    const std::size_t point_count,
    const std::size_t path_record_size,
    std::size_t& result) noexcept -> clipper_error_code {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (path_count > maximum / path_record_size) {
        return clipper_error_code::resource_limit;
    }
    result = path_count * path_record_size;
    if (point_count > (maximum - result) / sizeof(Point64)) {
        return clipper_error_code::resource_limit;
    }
    result += point_count * sizeof(Point64);
    return clipper_error_code::ok;
}

auto add_workspace_bytes(
    const std::size_t first,
    const std::size_t second,
    std::size_t& result) noexcept -> clipper_error_code {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (second > maximum - first) {
        return clipper_error_code::resource_limit;
    }
    result = first + second;
    return clipper_error_code::ok;
}

}  // namespace clipper2next::internal
