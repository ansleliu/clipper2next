#include "support/private/engine_resource_plan.h"

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

auto plan_offset_generation_resources(
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
    if (input_point_count == 0U) { return {}; }
    const auto arc = join_type == JoinType::Round || end_type == EndType::Round
        ? make_arc_parameters(delta, arc_tolerance, arc_segments_per_quadrant)
        : offset_arc_parameters{};
    const auto join_bound = offset_join_point_bound(join_type, arc);
    const auto single_bound = join_type == JoinType::Round ? offset_circle_steps(arc, delta) : std::size_t{4U};
    // Polygon has one join per vertex. Joined emits both orientations; open
    // paths emit two interior sides and two caps (at most pi each).
    const auto cap_bound = end_type == EndType::Round
        ? saturating_add(arc_step_count(arc, pi), 1U) : std::size_t{2U};
    const auto sided_bound = end_type == EndType::Polygon ? join_bound
        : std::max(saturating_product(join_bound, 2U), cap_bound);
    const auto expansion = std::max(single_bound, sided_bound);
    const auto point_bound =
        saturating_product(input_point_count, expansion);
    constexpr auto direct_path_scan_limit = std::size_t{512U};
    const auto direct_preparation_work = saturating_product(
        input_path_count,
        saturating_product(
            direct_path_scan_limit, direct_path_scan_limit));
    const auto work = saturating_add(
        saturating_add(
            saturating_product(point_bound, 4U),
            saturating_product(input_point_count, 8U)),
        saturating_add(
            saturating_product(input_path_count, input_path_count),
            direct_preparation_work));
    const auto retained_output_bytes = saturating_add(saturating_product(
        point_bound, sizeof(Point64)), saturating_product(input_path_count, 2U * sizeof(geotypes::PathDescriptor)));
    const auto per_task_scratch_bytes = saturating_add(
        sizeof(offset_state),
        saturating_add(
            saturating_product(
                maximum_path_point_count, sizeof(PointD) + 2U * sizeof(Point64)),
            saturating_product(
                saturating_product(maximum_path_point_count, expansion),
                sizeof(Point64))));
    const auto direct_candidate_bytes = saturating_add(
        saturating_product(retained_output_bytes, 2U),
        saturating_add(
            saturating_product(
                input_path_count,
                sizeof(Rect64) + 3U * sizeof(std::size_t) +
                    sizeof(geotypes::PathDescriptor)),
            saturating_product(
                maximum_path_point_count, 2U * sizeof(Point64))));
    auto plan = engine_resource_plan{
        .work = work,
        .workspace_bytes = std::max(
            direct_candidate_bytes,
            saturating_add(
                retained_output_bytes, per_task_scratch_bytes)),
    };
    plan.retained_output_bytes = retained_output_bytes;
    plan.scratch_bytes = per_task_scratch_bytes;
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
    const auto parallel_phase_bytes = saturating_add(
        planning_bytes,
        saturating_add(
            retained_output_bytes,
            saturating_product(
                active_concurrency, per_task_scratch_bytes)));
    const auto merge_phase_bytes = saturating_add(
        planning_bytes,
        saturating_product(retained_output_bytes, 2U));
    plan.workspace_bytes = std::max(
        plan.workspace_bytes,
        std::max(parallel_phase_bytes, merge_phase_bytes));
    return plan;
}

auto finalize_offset_engine_resources(
    offset_engine_resource_context& context,
    const std::size_t output_path_count,
    const std::size_t output_point_count,
    const bool requires_cleanup) noexcept -> clipper_error_code {
    context.selected = context.generation;
    context.selected.work = context.work;
    context.selected.workspace_bytes = std::max(context.selected.workspace_bytes, context.engine_high_water);
    if (requires_cleanup) {
        const auto cleanup = plan_clip_engine_resources(output_point_count);
        auto retained_output_bytes = std::size_t{};
        if (measure_offset_path_storage(
                output_path_count,
                output_point_count,
                sizeof(geotypes::PathDescriptor),
                retained_output_bytes) != clipper_error_code::ok) {
            return clipper_error_code::resource_limit;
        }
        context.selected.work = saturating_add(
            context.work, cleanup.work);
        context.selected.workspace_bytes = std::max(
            context.selected.workspace_bytes,
            saturating_add(
                retained_output_bytes, cleanup.workspace_bytes));
    }
    return context.selected.work == (std::numeric_limits<std::size_t>::max)() ||
            context.selected.workspace_bytes == (std::numeric_limits<std::size_t>::max)() ||
            context.selected.work > context.maximum_work ||
            context.selected.workspace_bytes >
                context.maximum_workspace_bytes
        ? clipper_error_code::resource_limit
        : clipper_error_code::ok;
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

}  // namespace clipper2next::internal
