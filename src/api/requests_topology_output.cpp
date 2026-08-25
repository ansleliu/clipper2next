#include "api/private/borrowed_topology_pipeline.h"

#include "clip/engine/private/engine_output_cleanup.h"
#include "clip/engine/private/engine_path_builder.h"
#include "support/private/checked_size.h"

#include <algorithm>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto record_depth(const output_record_node& record,
                                std::size_t record_limit,
                                std::size_t& depth) noexcept -> clipper_error_code {
    depth = 0U;
    auto* owner = record.owner.get();
    while (owner) {
        if (++depth > record_limit) { return clipper_error_code::internal_error; }
        owner = owner->owner.get();
    }
    return clipper_error_code::ok;
}

auto prepare_output_records(OutRecList& records,
                            const engine_output_cleanup_options& options) -> void {
    for (const auto& record_ref : records) {
        auto* record = record_ref.get();
        if (record && !record->is_open && record->pts) {
            static_cast<void>(check_bounds(records, record, options));
        }
    }
    for (const auto& record_ref : records) {
        auto* record = record_ref.get();
        if (record && !record->is_open && record->pts && !record->bounds.is_empty()) {
            resolve_output_record_owner(records, record, options);
        }
    }
}

[[nodiscard]] auto collect_record_metadata(
    const OutRecList& records,
    const execution_options& options,
    const borrowed_clip_limits64& limits,
    borrowed_topology_workspace& workspace,
    std::size_t& maximum_ring_point_count,
    std::size_t& reallocation_count) -> clipper_error_code {
    resize_and_count_growth(workspace.record_metadata, records.size(), reallocation_count);
    reserve_and_count_growth(workspace.polygon_layouts, records.size(), reallocation_count);
    maximum_ring_point_count = 0U;

    for (const auto& record_ref : records) {
        auto* record = record_ref.get();
        if (!record || record->is_open || !record->pts || record->bounds.is_empty()) { continue; }
        std::size_t depth = 0U;
        if (const auto error = record_depth(*record, records.size(), depth);
            error != clipper_error_code::ok) {
            return error;
        }
        if (record->idx >= workspace.record_metadata.size()) {
            return clipper_error_code::internal_error;
        }
        auto& metadata = workspace.record_metadata[record->idx];
        metadata.point_count = path64_point_count(record->pts, options.reverse_solution, false);
        if (metadata.point_count == 0U) { return clipper_error_code::internal_error; }
        maximum_ring_point_count = (std::max)(maximum_ring_point_count, metadata.point_count);
        if ((depth & 1U) == 0U) {
            if (workspace.polygon_layouts.size() >= limits.maximum_output_polygon_count) {
                return clipper_error_code::resource_limit;
            }
            metadata.polygon_index = workspace.polygon_layouts.size();
            workspace.polygon_layouts.push_back(
                {.ring_count = 1U, .point_count = metadata.point_count});
        } else {
            metadata.is_hole = true;
        }
    }
    return clipper_error_code::ok;
}

[[nodiscard]] auto assign_polygon_relationships(
    const OutRecList& records, borrowed_topology_workspace& workspace) -> clipper_error_code {
    for (const auto& record_ref : records) {
        auto* shell = record_ref.get();
        if (!shell || shell->idx >= workspace.record_metadata.size()) { continue; }
        const auto& metadata = workspace.record_metadata[shell->idx];
        if (metadata.point_count == 0U || metadata.is_hole || !shell->owner) { continue; }
        auto* parent_shell = shell->owner->owner.get();
        if (!parent_shell) { continue; }
        if (parent_shell->idx >= workspace.record_metadata.size()) {
            return clipper_error_code::internal_error;
        }
        const auto parent_index = workspace.record_metadata[parent_shell->idx].polygon_index;
        if (parent_index == topology_no_polygon_index) {
            return clipper_error_code::internal_error;
        }
        workspace.polygon_layouts[metadata.polygon_index].parent_polygon_index = parent_index;
    }

    for (const auto& record_ref : records) {
        auto* hole = record_ref.get();
        if (!hole || hole->idx >= workspace.record_metadata.size()) { continue; }
        const auto& metadata = workspace.record_metadata[hole->idx];
        if (metadata.point_count == 0U || !metadata.is_hole) { continue; }
        auto* shell = hole->owner.get();
        if (!shell || shell->idx >= workspace.record_metadata.size()) {
            return clipper_error_code::internal_error;
        }
        const auto polygon_index = workspace.record_metadata[shell->idx].polygon_index;
        if (polygon_index == topology_no_polygon_index ||
            polygon_index >= workspace.polygon_layouts.size()) {
            return clipper_error_code::internal_error;
        }
        auto& layout = workspace.polygon_layouts[polygon_index];
        layout.ring_count = checked_size_add(layout.ring_count, 1U);
        layout.point_count = checked_size_add(layout.point_count, metadata.point_count);
    }
    return clipper_error_code::ok;
}

[[nodiscard]] auto summarize_layout(const borrowed_topology_workspace& workspace,
                                    const borrowed_clip_limits64& limits,
                                    std::size_t& ring_count) -> clipper_error_code {
    ring_count = 0U;
    std::size_t point_count = 0U;
    for (const auto& layout : workspace.polygon_layouts) {
        ring_count = checked_size_add(ring_count, layout.ring_count);
        point_count = checked_size_add(point_count, layout.point_count);
    }
    if (exceeds(ring_count, limits.maximum_output_ring_count) ||
        exceeds(point_count, limits.maximum_output_point_count)) {
        return clipper_error_code::resource_limit;
    }
    return clipper_error_code::ok;
}

auto build_ring_order(const OutRecList& records,
                      std::size_t ring_count,
                      borrowed_topology_workspace& workspace,
                      std::size_t& reallocation_count) -> void {
    resize_and_count_growth(workspace.next_ring_by_polygon,
                            checked_size_add(workspace.polygon_layouts.size(), 1U),
                            reallocation_count);
    workspace.next_ring_by_polygon[0] = 0U;
    for (std::size_t index = 0U; index < workspace.polygon_layouts.size(); ++index) {
        workspace.next_ring_by_polygon[index + 1U] = checked_size_add(
            workspace.next_ring_by_polygon[index], workspace.polygon_layouts[index].ring_count);
    }
    resize_and_count_growth(workspace.ring_descriptors, ring_count, reallocation_count);
    for (const auto& record_ref : records) {
        auto* shell = record_ref.get();
        if (!shell || shell->idx >= workspace.record_metadata.size()) { continue; }
        const auto& metadata = workspace.record_metadata[shell->idx];
        if (metadata.point_count == 0U || metadata.is_hole) { continue; }
        workspace.ring_descriptors[workspace.next_ring_by_polygon[metadata.polygon_index]++] = {
            shell, metadata.polygon_index, topology_ring_role::shell, metadata.point_count};
    }
    for (const auto& record_ref : records) {
        auto* hole = record_ref.get();
        if (!hole || hole->idx >= workspace.record_metadata.size()) { continue; }
        const auto& metadata = workspace.record_metadata[hole->idx];
        if (metadata.point_count == 0U || !metadata.is_hole) { continue; }
        const auto polygon_index = workspace.record_metadata[hole->owner->idx].polygon_index;
        workspace.ring_descriptors[workspace.next_ring_by_polygon[polygon_index]++] = {
            hole, polygon_index, topology_ring_role::hole, metadata.point_count};
    }
}

[[nodiscard]] auto measure_workspace(std::size_t input_workspace_bytes,
                                     const borrowed_clip_limits64& limits,
                                     const borrowed_topology_workspace& workspace,
                                     std::size_t& peak_workspace_bytes) -> clipper_error_code {
    auto retained = input_workspace_bytes;
    if (auto error = checked_workspace_add(
            retained, workspace.polygon_layouts.size(), sizeof(topology_polygon_layout64));
        error != clipper_error_code::ok) {
        return error;
    }
    if (auto error = checked_workspace_add(
            retained, workspace.ring_descriptors.size(), sizeof(topology_ring_descriptor64));
        error != clipper_error_code::ok) {
        return error;
    }
    auto building = retained;
    if (auto error = checked_workspace_add(
            building, workspace.record_metadata.size(), sizeof(topology_record_metadata64));
        error != clipper_error_code::ok) {
        return error;
    }
    if (auto error = checked_workspace_add(
            building, workspace.next_ring_by_polygon.size(), sizeof(std::size_t));
        error != clipper_error_code::ok) {
        return error;
    }
    peak_workspace_bytes = (std::max)(input_workspace_bytes, building);
    return exceeds(peak_workspace_bytes, limits.maximum_staging_workspace_bytes)
               ? clipper_error_code::resource_limit
               : clipper_error_code::ok;
}

}  // namespace

auto build_topology_descriptors(engine_execution_context& context,
                                const execution_options& options,
                                const borrowed_clip_limits64& limits,
                                std::size_t input_workspace_bytes,
                                borrowed_topology_workspace& workspace,
                                std::size_t& maximum_ring_point_count,
                                std::size_t& peak_workspace_bytes,
                                std::size_t& reallocation_count) -> clipper_error_code {
    auto& records = context.output_owner().records();
    engine_output_cleanup_options cleanup_options;
    cleanup_options.preserve_collinear = options.preserve_collinear;
    cleanup_options.reverse_solution = options.reverse_solution;
    cleanup_options.using_polytree = true;
    cleanup_options.path_storage = output_path_storage::linked_points;
    prepare_output_records(records, cleanup_options);
    if (const auto error = collect_record_metadata(
            records, options, limits, workspace, maximum_ring_point_count, reallocation_count);
        error != clipper_error_code::ok) {
        return error;
    }
    if (const auto error = assign_polygon_relationships(records, workspace);
        error != clipper_error_code::ok) {
        return error;
    }
    std::size_t ring_count = 0U;
    if (const auto error = summarize_layout(workspace, limits, ring_count);
        error != clipper_error_code::ok) {
        return error;
    }
    build_ring_order(records, ring_count, workspace, reallocation_count);
    return measure_workspace(input_workspace_bytes, limits, workspace, peak_workspace_bytes);
}

}  // namespace clipper2next::internal
