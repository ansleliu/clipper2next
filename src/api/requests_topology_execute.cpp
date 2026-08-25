#include "api/private/borrowed_topology_pipeline.h"
#include "api/private/engine_resource_plan.h"

#include "clip/engine/private/engine_path_builder.h"
#include "clip/engine/private/engine_scanbeam_processor.h"
#include "clip/engine/private/engine_scanbeam_services.h"
#include "clip/private/borrowed_topology_access.h"
#include "support/private/checked_size.h"

namespace clipper2next::internal {
namespace {

class writer_transaction final {
public:
    explicit writer_transaction(topology_writer64& writer) noexcept : writer_(&writer) {}
    writer_transaction(const writer_transaction&) = delete;
    auto operator=(const writer_transaction&) -> writer_transaction& = delete;
    ~writer_transaction() {
        if (active_) { topology_writer64_access::cancel(*writer_); }
    }

    auto activate() noexcept -> void { active_ = true; }
    auto commit() noexcept -> void { active_ = false; }

private:
    topology_writer64* writer_;
    bool active_{};
};

[[nodiscard]] auto measure_input(const borrowed_clip_request64& request,
                                 borrowed_topology_workspace& workspace,
                                 topology_write_stats64& stats,
                                 std::size_t& workspace_bytes) -> clipper_error_code {
    if (const auto error = measure_paths(request.subjects,
                                         workspace.subjects,
                                         request.limits,
                                         stats.input_path_count,
                                         stats.input_point_count,
                                         stats.staging_reallocation_count);
        error != clipper_error_code::ok) {
        return error;
    }
    if (const auto error = measure_paths(request.clips,
                                         workspace.clips,
                                         request.limits,
                                         stats.input_path_count,
                                         stats.input_point_count,
                                         stats.staging_reallocation_count);
        error != clipper_error_code::ok) {
        return error;
    }

    const auto vertex_capacity =
        checked_size_add(workspace.subjects.source_point_count,
                         workspace.clips.source_point_count);
    if (auto error = checked_workspace_add(workspace_bytes, vertex_capacity, sizeof(Vertex));
        error != clipper_error_code::ok) {
        return error;
    }
    const auto path_count = checked_size_add(
        workspace.subjects.paths.size(), workspace.clips.paths.size());
    if (auto error = checked_workspace_add(
            workspace_bytes,
            path_count,
            sizeof(path_source_contract::borrowed_path_measurement64));
        error != clipper_error_code::ok) {
        return error;
    }
    return exceeds(workspace_bytes, request.limits.maximum_staging_workspace_bytes)
               ? clipper_error_code::resource_limit
               : clipper_error_code::ok;
}

[[nodiscard]] auto load_input(const borrowed_clip_request64& request,
                              const borrowed_topology_workspace& workspace,
                              clipper_base_state& state,
                              topology_write_stats64& stats) -> clipper_error_code {
    if (const auto error =
            load_paths(state, request.subjects, workspace.subjects, PathType::Subject, stats);
        error != clipper_error_code::ok) {
        return error;
    }
    return load_paths(state, request.clips, workspace.clips, PathType::Clip, stats);
}

[[nodiscard]] auto run_engine(const borrowed_clip_request64& request,
                              clipper_base_state& state,
                              engine_execution_context& context) -> bool {
    engine_scanbeam_processor processor{context};
    engine_scanbeam_orchestration_options orchestration_options;
    orchestration_options.preserve_collinear = request.options.preserve_collinear;
    orchestration_options.has_open_paths = false;
    bool succeeded = true;
    engine_intersection_services intersection_services;
    intersection_services.intersection_policy = request.options.intersection_policy;
    engine_scanbeam_services services{
        state, orchestration_options, succeeded, intersection_services};
    return processor.execute(services, request.clip_type, request.fill_rule, true);
}

}  // namespace

auto write_topology(topology_writer64& writer,
                    const execution_options& options,
                    std::span<const topology_polygon_layout64> polygon_layouts,
                    std::span<const topology_ring_descriptor64> ring_descriptors,
                    std::size_t maximum_ring_point_count,
                    std::size_t previous_peak_workspace_bytes,
                    topology_write_stats64& stats) -> clipper_error_code {
    stats.peak_workspace_bytes = previous_peak_workspace_bytes;
    topology_layout64 layout;
    layout.polygons = polygon_layouts;
    layout.ring_count = ring_descriptors.size();
    for (const auto& polygon : polygon_layouts) {
        layout.point_count = checked_size_add(layout.point_count, polygon.point_count);
    }
    layout.maximum_ring_point_count = maximum_ring_point_count;
    layout.staging_workspace_bytes = stats.peak_workspace_bytes;

    writer_transaction transaction{writer};
    transaction.activate();
    if (const auto error = topology_writer64_access::begin(writer, layout);
        error != clipper_error_code::ok) {
        return error;
    }
    for (const auto& descriptor : ring_descriptors) {
        const topology_ring_layout64 ring{
            descriptor.polygon_index, descriptor.role, descriptor.point_count};
        auto destination = std::span<geotypes::Point2i64>{};
        if (const auto error = topology_writer64_access::acquire(writer, ring, destination);
            error != clipper_error_code::ok) {
            return error;
        }
        if (destination.size() != descriptor.point_count) {
            return clipper_error_code::sink_failure;
        }
        ++stats.output_ring_acquire_count;
        if (!build_path64_into(
                descriptor.record->pts, options.reverse_solution, false, destination)) {
            return clipper_error_code::internal_error;
        }
        stats.output_final_point_writes =
            checked_size_add(stats.output_final_point_writes, destination.size());
    }
    if (const auto error = topology_writer64_access::finish(writer);
        error != clipper_error_code::ok) {
        return error;
    }
    transaction.commit();
    return clipper_error_code::ok;
}

auto execute_borrowed_topology(const borrowed_clip_request64& request,
                               topology_writer64& writer)
    -> clipper_result<topology_write_stats64> {
    if (!topology_writer64_access::is_bound(writer)) {
        return make_clipper_error<topology_write_stats64>(clipper_error_code::sink_failure);
    }

    topology_write_stats64 stats;
    borrowed_topology_engine_state_lease lease;
    auto& workspace = lease.workspace();
    std::size_t input_workspace_bytes = 0U;
    if (const auto error = measure_input(request, workspace, stats, input_workspace_bytes);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }
    const auto engine_plan = plan_clip_engine_resources(stats.input_point_count);
    stats.planned_engine_work = engine_plan.work;
    stats.planned_engine_workspace_bytes = engine_plan.workspace_bytes;
    if (engine_plan.work > request.limits.maximum_engine_work ||
        engine_plan.workspace_bytes >
            request.limits.maximum_engine_workspace_bytes) {
        return make_clipper_error<topology_write_stats64>(
            clipper_error_code::resource_limit);
    }
    auto& state = lease.state();
    if (const auto error = load_input(request, workspace, state, stats);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }

    engine_execution_context context{state};
    if (!run_engine(request, state, context)) {
        return make_clipper_error<topology_write_stats64>(clipper_error_code::internal_error);
    }

    std::size_t maximum_ring_point_count = 0U;
    std::size_t peak_workspace_bytes = 0U;
    if (const auto error = build_topology_descriptors(context,
                                                      request.options,
                                                      request.limits,
                                                      input_workspace_bytes,
                                                      workspace,
                                                      maximum_ring_point_count,
                                                      peak_workspace_bytes,
                                                      stats.staging_reallocation_count);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }

    stats.output_polygon_count = workspace.polygon_layouts.size();
    stats.output_ring_count = workspace.ring_descriptors.size();
    for (const auto& polygon : workspace.polygon_layouts) {
        stats.output_point_count = checked_size_add(stats.output_point_count, polygon.point_count);
    }
    if (const auto error = write_topology(writer,
                                          request.options,
                                          workspace.polygon_layouts,
                                          workspace.ring_descriptors,
                                          maximum_ring_point_count,
                                          peak_workspace_bytes,
                                          stats);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }
    return stats;
}

}  // namespace clipper2next::internal
